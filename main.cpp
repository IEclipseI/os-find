//
// Created by roman on 10.04.19.
//

#include <iostream>
#include <string>
#include <dirent.h>
#include <sys/types.h>
#include <cstring>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include <wait.h>

using std::string;
using std::cout;
using std::vector;
using std::function;
using std::shared_ptr;
using std::unordered_map;
using std::cerr;

void show_help() {
    static constexpr char help_msg[] = "usage:\n"
                                       "os_find [directory] [options]...\n"
                                       "options:\n"
                                       "-help - show help (does not do search)\n"
                                       "-inum [num] - search files with inode number equals to num\n"
                                       "-name [filename] - search files with name equals to filename\n"
                                       "-size [-=+][size] - search files with size less(-), equals(=) or not less(+) than size\n"
                                       "-nlinks [num] - search files with hardlinks number equals to num\n"
                                       "-exec [program_path] - execute program_path program with each found file as argument. Only one program can be executed after search\n";
    cout << help_msg << std::endl;
}

static unordered_map<string, size_t> commands{
        {"-help",   0},
        {"-inum",   1},
        {"-name",   1},
        {"-size",   1},
        {"-nlinks", 1},
        {"-exec",   1},
};

shared_ptr<vector<function<bool(string &)>>> extract_predicates(size_t argc, char *argv[]) {
    shared_ptr<vector<function<bool(string &)>>> predicates(new vector<function<bool(string &)>>);
    for (size_t i = 0; i < argc;) {
        size_t args_count;
        try {
            if (std::strcmp(argv[i], "-help") == 0) {
                return nullptr;
            } else if (std::strcmp(argv[i], "-inum") == 0) {
                args_count = 1;
                if (i + args_count >= argc)
                    return nullptr;
                int inode_num = std::stoi(argv[i + 1]);
                predicates->emplace_back([inode_num](string &a) {
                    struct stat st{};
                    if (stat(a.c_str(), &st) == -1) {
                        cerr << "Error with file: " << a << ", problem: " << strerror(errno) << std::endl;
                        return false;
                    }
                    return st.st_ino == inode_num;
                });
            } else if (std::strcmp(argv[i], "-size") == 0) {
                args_count = 1;
                if (i + args_count >= argc)
                    return nullptr;
                if (strlen(argv[i + 1]) < 2)
                    return nullptr;
                int size = std::stoi(argv[i + 1] + 1);
                if (argv[i + 1][0] == '+') {
                    predicates->emplace_back([size](string &a) {
                        struct stat st{};
                        if (stat(a.c_str(), &st) == -1) {
                            cerr << "Error with file: " << a << ", problem: " << strerror(errno) << std::endl;
                            return false;
                        }
                        return st.st_size >= size;
                    });
                } else if (argv[i + 1][0] == '=') {
                    predicates->emplace_back([size](string &a) {
                        struct stat st{};
                        if (stat(a.c_str(), &st) == -1) {
                            cerr << "Error with file: " << a << ", problem: " << strerror(errno) << std::endl;
                            return false;
                        }
                        return st.st_size == size;
                    });
                } else if (argv[i + 1][0] == '-') {
                    predicates->emplace_back([size](string &a) {
                        struct stat st{};
                        if (stat(a.c_str(), &st) == -1) {
                            cerr << "Error with file: " << a << ", problem: " << strerror(errno) << std::endl;
                            return false;
                        }
                        return st.st_size <= size;
                    });
                } else {
                    return nullptr;
                }
            } else if (std::strcmp(argv[i], "-nlinks") == 0) {
                args_count = 1;
                if (i + args_count >= argc)
                    return nullptr;
                int links_num = std::stoi(argv[i + 1]);
                predicates->emplace_back([links_num](string &a) {
                    struct stat st{};
                    if (stat(a.c_str(), &st) == -1) {
                        cerr << "Error with file: " << a << ", problem: " << strerror(errno) << std::endl;
                        return false;
                    }
                    return st.st_nlink == links_num;
                });
            } else if (std::strcmp(argv[i], "-name") == 0) {
                args_count = 1;
                if (i + args_count >= argc)
                    return nullptr;
                string filename = argv[i + 1];
                predicates->emplace_back([filename](string &a) {
                    struct stat st{};
                    if (stat(a.c_str(), &st) == -1) {
                        cerr << "Error with file: " << a << ", problem: " << strerror(errno) << std::endl;
                        return false;
                    }
                    int j = a.size() - 1;
                    for (; j >= 0 && a[j] != '/'; --j);
                    if (j >= 0) {
                        return a.substr(j + 1, a.size()) == filename;
                    }
                    return false;
                });
            } else if (std::strcmp(argv[i], "-exec") == 0) {
                args_count = 1;
                //not a predicate;
            } else {
                return nullptr;
            }
            i += args_count + 1;
        } catch (...) {
            return nullptr;
        }
    }
    return predicates;
}

char *extract_exec(size_t argc, char *argv[]) {
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-exec") == 0) {
            if (i + 1 >= argc) {
                show_help();
                return nullptr;
            }
            return argv[i + 1];
        }
    }
    return nullptr;
}

void recursive_walk(const char *path, vector<string> &files, shared_ptr<vector<function<bool(string &)>>> &predicates) {
    DIR *dir;
    dirent *entry;
    if (!(dir = opendir(path)))
        return;

    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            string new_dir = path;
            new_dir.push_back('/');
            new_dir.append(entry->d_name);
            recursive_walk(new_dir.c_str(), files, predicates);
        } else if (entry->d_type == DT_REG) {
            bool good = true;
            auto filepath = string(path).append("/").append(entry->d_name);
            for (auto &pr : *predicates) {
                good &= pr(filepath);
                if (!good)
                    break;
            }
            if (good) {
                cout << filepath << std::endl;
                files.push_back(filepath);
            }
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        show_help();
        return 0;
    }
    auto predicates = extract_predicates(argc - 2, argv + 2);
    if (!predicates) {
        show_help();
        return 0;
    }
    char *execute = extract_exec(argc - 2, argv + 2);
    vector<string> files;
    recursive_walk(argv[1], files, predicates);
    if (execute) {
        for (auto &file : files) {
            cout << "Executing on " << file << std::endl;
            pid_t id = fork();
            if (id == -1) {
                cerr << "Cannot execute program: " << execute << ", problem: " << strerror(errno) << std::endl;
            } else {
                if (id == 0) {
                    vector<char *> exec_arg;
                    exec_arg.push_back(execute);
                    exec_arg.push_back(&file[0]);
                    exec_arg.push_back(nullptr);
                    if (execve(execute, exec_arg.data(), nullptr) == -1) {
                        cerr << "Error occurred:" << strerror(errno) << std::endl;
                        return -1;
                    }
                    return 0;
                } else {
                    int status;
                    if (waitpid(id, &status, 0) == -1) {
                        cerr << strerror(errno) << std::endl;
                    } else {
                        cout << "Exit status: " << status << std::endl;
                    }
                }
            }
        }
    }
    cout << "Files found: " << files.size() << std::endl;
    return 0;
}