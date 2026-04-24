#include <err.h>
#include <getopt.h>
#if defined(__has_include)
#if __has_include(<Bom/Bom.h>)
#include <Bom/Bom.h>
#else
#include "Bom.h"
#endif
#else
#include "Bom.h"
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

static void usage(int full) {
    fprintf(stderr, "Usage: %s [-h] [-s] directory bom\n", getprogname());
    fprintf(stderr, "       %s [-s] -i filelist bom\n", getprogname());

    if (full == 1) {
        fputs(
            "\n"
            "    -h              print full usage\n"
            "    -s              create a path-only bom\n"
            "    -i filelist     a file listing in lsbom(8) format used to create the bom file\n",
            stderr);
    }

    exit(1);
}

static void warn_insert_failure_and_free(BOMFSObject object) {
    const char *path = BOMFSObjectPathName(object);

    warnx("Can't insert %s", path);
    if (object) {
        BOMFSObjectFree(object);
    }
}

static void err_open_listing_file(void) {
    err(1, "Can't open \"%s\"", optarg);
}

int main(int argc, char **argv) {
    FILE *listing_fp = NULL;
    int remaining_argc = 0;
    bool close_listing_fp = false;
    const BOMFSOFlags all_data_flags = B_ALLDATA;
    const BOMFSOFlags path_only_flags = B_PATHONLY;
    BOMFSOFlags create_flags = B_ALLDATA;
    char **remaining_argv = NULL;

    if (argc == 1) {
        usage(1);
    }

    for (;;) {
        const int option = getopt_long(argc, argv, "hsi:", long_options, NULL);

        if (option == 'i') {
            if (strcmp("-", optarg) == 0) {
                listing_fp = stdin;
            } else {
                listing_fp = fopen(optarg, "r");
                close_listing_fp = true;
                if (!listing_fp) {
                    err_open_listing_file();
                }
            }
            continue;
        }

        if (option == -1) {
            break;
        }

        if (option == 's') {
            create_flags = path_only_flags;
            continue;
        }

        if (option == 'h') {
            usage(1);
        }

        usage(0);
    }

    remaining_argc = argc - optind;
    remaining_argv = &argv[optind];

    if (listing_fp) {
        BOMBom bom = NULL;
        const char *bom_path = NULL;
        size_t line_length = 0;
        char *line = NULL;
        int line_number = 1;

        if (remaining_argc != 1) {
            usage(0);
        }

        bom_path = remaining_argv[0];
        bom = BOMBomNewWithSys(bom_path, NULL);
        if (!bom) {
            errx(1, "can't make bom %s for listing", bom_path);
        }

        line = fgetln(listing_fp, &line_length);
        while (line) {
            char *cursor = line;

            if (line[line_length - 1] == '\n') {
                line[line_length - 1] = '\0';

                if (create_flags == all_data_flags) {
                    BOMFSObject object = BOMFSObjectParseSummary(cursor);

                    if (BOMBomInsertFSObject(bom, object, 1)) {
                        warn_insert_failure_and_free(object);
                    } else {
                        BOMFSObjectFree(object);
                    }
                } else {
                    char *path = strsep(&cursor, "\t");
                    const char *short_name = strrchr(path, '/');
                    BOMFSObject object = NULL;

                    short_name = short_name ? short_name + 1 : path;
                    if (!cursor) {
                        char *parent_path = strdup(path);
                        char *slash = strrchr(parent_path, '/');

                        if (slash) {
                            BOMFSObject parent = NULL;
                            const char *parent_short_name = NULL;

                            *slash = '\0';
                            slash = strrchr(parent_path, '/');
                            parent_short_name = slash ? slash + 1 : parent_path;

                            parent = BOMBomGetFSObjectAtPath(bom, parent_path);
                            if (parent) {
                                if (BOMFSObjectType(parent) != BOMFSOTypeDirectory) {
                                    if (BOMBomRemoveFSObject(bom, parent)) {
                                        warnx("Can't remove %s", parent_path);
                                    }

                                    BOMFSObjectFree(parent);
                                    parent = BOMFSObjectNewWithSys(BOMFSOTypeDirectory, NULL);
                                    BOMFSObjectSetFlags(parent, path_only_flags);
                                    BOMFSObjectSetPathName(parent, parent_path, true);
                                    BOMFSObjectSetShortName(parent, parent_short_name, true);
                                    if (BOMBomInsertFSObject(bom, parent, 0)) {
                                        warnx("Can't insert %s", parent_path);
                                    }
                                }

                                BOMFSObjectFree(parent);
                            } else {
                                warnx("Parent %s not present in bom", parent_path);
                            }
                        }

                        free(parent_path);
                        object = BOMFSObjectNewWithSys(BOMFSOTypeFile, NULL);
                        BOMFSObjectSetFlags(object, path_only_flags);
                        BOMFSObjectSetPathName(object, path, true);
                        BOMFSObjectSetShortName(object, short_name, true);
                        if (BOMBomInsertFSObject(bom, object, 1)) {
                            warn_insert_failure_and_free(object);
                        } else {
                            BOMFSObjectFree(object);
                        }
                    } else {
                        warnx("Malformed line %d has invalid information", line_number);
                    }
                }
            } else {
                warnx("Malformed line %d has invalid information", line_number);
            }

            ++line_number;
            line = fgetln(listing_fp, &line_length);
        }

        if (ferror(listing_fp)) {
            BOMBomFree(bom);
            unlink(bom_path);
        }

        BOMBomFree(bom);
        if (close_listing_fp) {
            fclose(listing_fp);
        }
    } else {
        BOMBom bom = NULL;

        if (remaining_argc != 2) {
            usage(0);
        }

        bom = BOMBomNewFromDirectoryWithSys(remaining_argv[1], remaining_argv[0], create_flags, NULL);
        if (!bom) {
            errx(1, "can't make bom %s for dir %s", remaining_argv[1], remaining_argv[0]);
        }

        BOMBomFree(bom);
    }

    return 0;
}
