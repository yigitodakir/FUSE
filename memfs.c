#define FUSE_USE_VERSION 30

#include <stdio.h>
#include <string.h>
#include <fuse.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>


#define TYPE_FILE 1
#define TYPE_DIRECTORY 2
#define TYPE_SOFTLINK 3
#define MAX_FILE_SIZE 512
#define MAX_FILENAME_LENGTH 255
#define MAX_NUMBER_OF_FILES 1024

typedef struct inode {
    int file_type; //1 for file, 2 for directory, 3 for links
    size_t file_size; //0 if the inode belongs to a directory
    mode_t permissions;
    time_t c_time;
    time_t m_time;
    time_t a_time;
    char data[MAX_FILE_SIZE]; //data stored in a file(only important in files)
    char name[MAX_FILENAME_LENGTH];
    int num_links;
    int number_children;
    uid_t user_id;
    gid_t group_id;
    struct inode *parent;
    struct inode **children;
    
} inode;

typedef enum LogType {
    CREATE_ROOT,
    CREATE_FILE,
    CREATE_DIRECTORY,
    CREATE_LINK,
    WRITE
} LogType;

inode* root;

inode* search(const char *path) {
    if(path == NULL) {
        return NULL;
    }

    if(strcmp(path, "/") == 0) {
        return root;
    }

    path++; //remove the leading /


    char *path_dup = strdup(path);
    char *split;
    split = strtok(path_dup, "/");

    inode* working_directory = root;

    while(split != NULL) {
        bool found = false;

        if(working_directory->file_type == TYPE_FILE) {
            free(path_dup);
            return NULL;
        } else {
            for(int i = 0; i < working_directory->number_children; i++) {
                if(strcmp(split, working_directory->children[i]->name) == 0) { 
                    working_directory = working_directory->children[i];
                    found = true;
                    break;
                }
            }

            if(found == false) {
                free(path_dup);
                return NULL;
            } else {
                split = strtok(NULL, "/");
            }

        }

    }

    free(path_dup);
    return working_directory;

}

void create_hierarchy(inode* parent_directory, inode* child) {
    int number = parent_directory->number_children + 1;

    parent_directory->children = realloc(parent_directory->children, number * sizeof(inode*));
    (parent_directory->children)[number - 1] = child;
    parent_directory->number_children = number;

}

void initialize_filesystem() {

    root = malloc(sizeof(inode));

    root->file_type = TYPE_DIRECTORY;
    root->file_size = 0;
    root->permissions = S_IFDIR | 0777;
    root->a_time = time(NULL);
    root->m_time = time(NULL);
    root->c_time = time(NULL);
    strcpy(root->name, "/");
    root->num_links = 2;
    root->number_children = 0;
    root->user_id = getuid();
    root->group_id = getgid();
    root->parent = NULL;
    root->children = NULL;

}


static int getattr_fuse(const char *path, struct stat *st) {

    inode* file_found = search(path);
    if(file_found == NULL) {
        return -ENOENT;
    }

    st->st_mode = file_found->permissions;
    st->st_nlink = file_found->num_links + file_found->number_children;
    st->st_uid = file_found->user_id;
    st->st_gid = file_found->group_id;
    st->st_size = file_found->file_size;
    st->st_atime = file_found->a_time;
    st->st_ctime = file_found->c_time;
    st->st_mtime = file_found->m_time;  

    return 0;

}

static int readdir_fuse(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    inode* directory = search(path);

    if(directory == NULL) {
        return -ENOENT;
    } else {
        if(directory->file_type == TYPE_FILE) {
            return -ENOENT;
        }

        for(int i = 0; i < directory->number_children; i++) {
            filler(buffer, directory->children[i]->name, NULL, 0);
        }

        directory->a_time = time(NULL);
    }
    
    return 0;
}

static int open_fuse(const char *path, struct fuse_file_info *fi) {
    inode* file_found = search(path);
    
    if(file_found == NULL) {
        return -ENOENT;
    }

    if(file_found->file_type == TYPE_DIRECTORY) {
        return -EISDIR;
    }

    file_found->a_time = time(NULL);
    return 0;
}

static int mkdir_fuse(const char *path, mode_t mode) {
    inode* new_dir = malloc(sizeof(inode));

    char *path1 = strdup(path);
    char* dir_name = strrchr(path1, '/') + 1;
    int parent_directory_length = (dir_name - path1) + 1;
    char* parent_directory = (char*) malloc(parent_directory_length);
    strncpy(parent_directory, path1, parent_directory_length);
    parent_directory[parent_directory_length - 1] = '\0';

    
    new_dir->file_type = TYPE_DIRECTORY;
    new_dir->file_size = 0;
    new_dir->permissions = S_IFDIR | 0777;
    new_dir->a_time = time(NULL);
    new_dir->m_time = time(NULL);
    new_dir->c_time = time(NULL);
    strcpy(new_dir->name, dir_name);
    new_dir->num_links = 2;
    new_dir->number_children = 0;
    new_dir->user_id = getuid();
    new_dir->group_id = getgid();
    new_dir->children = NULL;
    new_dir->parent = search(parent_directory);

    if(new_dir->parent == NULL) {
        return -ENOENT;
    }
    
    create_hierarchy(new_dir->parent, new_dir);

    free(path1);
    return 0;
}

static int create_fuse(const char *path, mode_t mode, struct fuse_file_info *fi) {
    inode* new_file = malloc(sizeof(inode));

    char *path1 = strdup(path);
    char* file_name = strrchr(path1, '/') + 1;
    int parent_directory_length = (file_name - path1) + 1;
    char* parent_directory = (char*) malloc(parent_directory_length);
    strncpy(parent_directory, path1, parent_directory_length);
    parent_directory[parent_directory_length - 1] = '\0';
    
    new_file->file_type = TYPE_FILE;
    new_file->file_size = 0;
    new_file->permissions = S_IFREG | 0777;
    new_file->a_time = time(NULL);
    new_file->m_time = time(NULL);
    new_file->c_time = time(NULL);
    strcpy(new_file->name, file_name);
    new_file->num_links = 0;
    new_file->number_children = 0;
    new_file->user_id = getuid();
    new_file->group_id = getgid();
    new_file->children = NULL;
    new_file->parent = search(parent_directory);

    if(new_file->parent == NULL) {
        return -ENOENT;
    }

    create_hierarchy(new_file->parent, new_file);

    free(path1);
    return 0;
}

static int mknod_fuse(const char *path, mode_t mode, dev_t device) {
    return create_fuse(path, mode, NULL);
}

static int read_fuse(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    
    inode* file_found = search(path);

    if(file_found == NULL) {
        return -ENOENT;
    } else if (file_found->file_type == TYPE_DIRECTORY) {
        return -ENOENT;
    } else {
        size_t toRead = size;
        if(offset + size > file_found->file_size) {
            toRead = (file_found->file_size) - offset;
        } 

        memcpy(buffer, (file_found->data) + offset, toRead);
        file_found->a_time = time(NULL);
        return toRead;
    }


}

static int write_fuse(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    inode* file_found = search(path);
    size_t toWrite = size;

    if(file_found == NULL) {
        return -ENOENT;
    } else if (file_found->file_type == TYPE_DIRECTORY) {
        return -ENOENT;
    } else {
        
        if(offset + size > MAX_FILE_SIZE) {
            toWrite = MAX_FILE_SIZE - offset;
        }

        memcpy((file_found->data) + offset, buffer, toWrite);
        file_found->file_size = (file_found->file_size) + toWrite;
        file_found->a_time = time(NULL);

        if(toWrite > 0) {
            file_found->m_time = time(NULL);
        }
        
    }

    return toWrite;
}

static int symlink_fuse(const char *path, const char *buffer) {
    inode* new_file = malloc(sizeof(inode));

    char *path1 = strdup(buffer);
    char* file_name = strrchr(path1, '/') + 1;
    int parent_directory_length = (file_name - path1) + 1;
    char* parent_directory = (char*) malloc(parent_directory_length);
    strncpy(parent_directory, path1, parent_directory_length);
    parent_directory[parent_directory_length - 1] = '\0';
    
    new_file->file_type = TYPE_SOFTLINK;
    new_file->file_size = strlen(path);
    new_file->permissions = S_IFLNK | 0777;
    new_file->a_time = time(NULL);
    new_file->m_time = time(NULL);
    new_file->c_time = time(NULL);
    strcpy(new_file->name, file_name);
    new_file->num_links = 0;
    new_file->number_children = 0;
    new_file->user_id = getuid();
    new_file->group_id = getgid();
    new_file->children = NULL;
    new_file->parent = search(parent_directory);

    if(new_file->parent == NULL) {
        return -ENOENT;
    }

    create_hierarchy(new_file->parent, new_file);

    if(new_file->file_size > MAX_FILE_SIZE) {
        return -EIO;
    }

    strncpy(new_file->data, path, strlen(path));

    free(path1);
    backup(CREATE_LINK, path, buffer, 0, 0);
    return 0;
    
}

static int readlink_fuse(const char *path, char *buffer, size_t size) {
    inode* link = search(path);
    if(link == NULL || link->file_type != TYPE_SOFTLINK) {
        return -ENOENT;
    }

    strncpy(buffer, link->data, link->file_size + 1);
    buffer[link->file_size] = '\0';
    link->a_time = time(NULL);
    return 0;
}


static struct fuse_operations fuse_ops = {
    .getattr = getattr_fuse,
    .read = read_fuse,
    .write = write_fuse,
    .readdir = readdir_fuse,
    .mkdir = mkdir_fuse,
    .mknod = mknod_fuse,
    .open = open_fuse,
    .create = create_fuse,
    .readlink = readlink_fuse,
    .symlink = symlink_fuse,
};

int main(int argc, char  *argv[]) {
    printf("Hello from %s. I got %d arguments\n", argv[1], argc);

    initialize_filesystem();
     
    return fuse_main(argc, argv, &fuse_ops, NULL);

}
