/* Host entry point for the direct ARM64 build.
 *
 * The game entry point is kept in src/melee/gm/gmmain.c. This file only
 * selects the game data location and starts that code. It does not emulate
 * the PowerPC CPU or load a translated executable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int MeleeMain(void);

static void print_usage(const char* program)
{
    fprintf(stderr,
            "Usage: %s [--root DIRECTORY | --disc IMAGE]\n"
            "       %s PATH\n\n"
            "PATH is treated as an extracted game directory or disc image.\n"
            "MELEE_GAME_ROOT and MELEE_DISC_IMAGE may also be set in the environment.\n",
            program, program);
}

static int set_data_path(const char* name, const char* path)
{
    struct stat info;

    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "%s requires a path\n", name);
        return 0;
    }
    if (stat(path, &info) != 0) {
        perror(path);
        return 0;
    }
    if (strcmp(name, "MELEE_GAME_ROOT") == 0) {
        if (!S_ISDIR(info.st_mode)) {
            fprintf(stderr, "%s must point to a directory: %s\n", name, path);
            return 0;
        }
    } else if (!S_ISREG(info.st_mode)) {
        fprintf(stderr, "%s must point to a regular image file: %s\n", name,
                path);
        return 0;
    }
    if (setenv(name, path, 1) != 0) {
        perror(name);
        return 0;
    }
    return 1;
}

int main(int argc, char** argv)
{
    const char* positional = NULL;
    int path_was_set = 0;

    for (int i = 1; i < argc; i++) {
        const char* argument = argv[i];
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argument, "--root") == 0 || strcmp(argument, "--disc") == 0) {
            if (i + 1 >= argc || !set_data_path(
                                     strcmp(argument, "--root") == 0
                                         ? "MELEE_GAME_ROOT"
                                         : "MELEE_DISC_IMAGE",
                                     argv[++i])) {
                print_usage(argv[0]);
                return 2;
            }
            path_was_set = 1;
            continue;
        }
        if (argument[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argument);
            print_usage(argv[0]);
            return 2;
        }
        if (positional != NULL) {
            fprintf(stderr, "Only one game path is allowed\n");
            print_usage(argv[0]);
            return 2;
        }
        positional = argument;
    }

    if (positional != NULL) {
        struct stat info;
        if (stat(positional, &info) != 0) {
            perror(positional);
            return 2;
        }
        if (!set_data_path(S_ISDIR(info.st_mode) ? "MELEE_GAME_ROOT"
                                                : "MELEE_DISC_IMAGE",
                           positional)) {
            return 2;
        }
        path_was_set = 1;
    }

    if (!path_was_set && getenv("MELEE_GAME_ROOT") == NULL &&
        getenv("MELEE_DISC_IMAGE") == NULL) {
        fprintf(stderr, "A game directory or disc image is required\n");
        print_usage(argv[0]);
        return 2;
    }

    return MeleeMain();
}
