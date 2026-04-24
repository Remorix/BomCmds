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
#include <libkern/OSByteOrder.h>
#include <mach/machine.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_endian.h>
#include <sys/stat.h>

struct nx_arch_info_compat {
    const char *name;
    cpu_type_t cputype;
};
extern const struct nx_arch_info_compat *NXGetArchInfoFromName(const char *name);

struct lsbom_options {
    bool paths_only;
    bool show_modified_times;
    bool suppress_modes;
    bool list_files;
    bool list_dirs;
    bool list_symlinks;
    bool list_block_devices;
    bool list_char_devices;
    bool type_filters_enabled;
    bool dump_bom_internals;
    bool have_arch_filter;
    bool have_custom_format;
    cpu_type_t arch_cputype;
    const char *custom_format;
};

struct tag_string_table {
    uint32_t count;
    char **entries;
};

struct tag_count_table {
    uint32_t count;
    uint32_t *counts;
};

struct metabom_cache {
    struct tag_string_table *project_tags;
    struct tag_string_table *package_tags;
};

enum print_field_bits {
    PRINT_FIELD_NAME = 0x0001,
    PRINT_FIELD_UNDOCUMENTED_L = 0x0002,
    PRINT_FIELD_MODE = 0x0004,
    PRINT_FIELD_SYMBOLIC_MODE = 0x0008,
    PRINT_FIELD_GROUP_ID = 0x0010,
    PRINT_FIELD_GROUP_NAME = 0x0020,
    PRINT_FIELD_USER_ID = 0x0040,
    PRINT_FIELD_USER_NAME = 0x0080,
    PRINT_FIELD_MTIME = 0x0100,
    PRINT_FIELD_FORMATTED_MTIME = 0x0200,
    PRINT_FIELD_SIZE = 0x0400,
    PRINT_FIELD_FORMATTED_SIZE = 0x0800,
    PRINT_FIELD_CHECKSUM = 0x1000,
    PRINT_FIELD_UID_GID = 0x2000,
    PRINT_FIELD_USER_GROUP_NAME = 0x4000,
};

static const struct option long_options[] = {
    {"arch", required_argument, NULL, 'a'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

static void usage(int full) {
    fprintf(
        stderr,
        "Usage: %s [-h] [-s] [-f] [-d] [-l] [-b] [-c] [-m] [-x]\n"
        "    [--arch archVal] [-p parameters] bom ...\n",
        getprogname());

    if (full == 1) {
        fputs(
            "\n"
            "    -h              print full usage\n"
            "    -s              print pathnames only\n"
            "    -f              list files\n"
            "    -d              list directories\n"
            "    -l              list symbolic links\n"
            "    -b              list block devices\n"
            "    -c              list character devices\n"
            "    -m              print modified times\n"
            "    -x              suppress modes for directories and symlinks\n"
            "    --arch archVal  print info for architecture archVal (\"ppc\", \"i386\", \"hppa\", \"sparc\", etc)\n"
            "    -p parameters   print only some of the results.  EACH OPTION CAN ONLY BE USED ONCE\n"
            "\t\tParameters:\n"
            "\t\t\tf\tfile name\n"
            "\t\t\tF\tfile name with quotes (i.e. \"/usr/bin/lsbom\")\n"
            "\t\t\tm\tfile mode (permissions)\n"
            "\t\t\tM\tsymbolic file mode\n"
            "\t\t\tg\tgroup id\n"
            "\t\t\tG\tgroup name\n"
            "\t\t\tu\tuser id\n"
            "\t\t\tU\tuser name\n"
            "\t\t\tt\tmod time\n"
            "\t\t\tT\tformatted mod time\n"
            "\t\t\ts\tfile size\n"
            "\t\t\tS\tformatted size\n"
            "\t\t\tc\t32-bit checksum\n"
            "\t\t\t/\tuser id/group id\n"
            "\t\t\t?\tuser name/group name\n",
            stderr);
    }

    exit(1);
}

static void normalize_legacy_long_options(int argc, char *argv[]) {
    static const struct {
        const char *legacy;
        const char *canonical;
    } rewrite_map[] = {
        {"-arch", "--arch"},
        {"-lang", "--lang"},
        {"-bom", "--bom"},
        {"-outBom", "--outBom"},
        {"-keepBinariesList", "--keepBinariesList"},
        {"-keepBinariesPattern", "--keepBinariesPattern"},
        {"-keepBinaries", "--keepBinaries"},
        {"-bomThin", "--bomThin"},
        {"-rsrc", "--rsrc"},
        {"-rsrcFork", "--rsrc"},
    };
    size_t j = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i - 1], "--") == 0) {
            continue;
        }

        for (j = 0; j < sizeof(rewrite_map) / sizeof(rewrite_map[0]); ++j) {
            if (strcmp(argv[i], rewrite_map[j].legacy) == 0) {
                argv[i] = (char *)rewrite_map[j].canonical;
                break;
            }
        }
    }
}

static bool mark_print_field(uint16_t *mask, uint16_t bit) {
    if ((*mask & bit) != 0) {
        fwrite("EACH OPTION CAN ONLY BE USED ONCE.\n", 0x23u, 1u, stderr);
        return false;
    }

    *mask |= bit;
    return true;
}

static bool validate_print_format(const char *format, uint16_t *mask_out) {
    uint16_t mask = 0;

    for (const unsigned char *p = (const unsigned char *)format; *p != '\0'; ++p) {
        uint16_t bit = 0;

        switch (*p) {
        case 'F':
        case 'f':
            bit = PRINT_FIELD_NAME;
            break;
        case 'L':
        case 'l':
            /*
         * This pair is accepted by the binary's validator but does not appear
         * in the usage string. Keep the slot reserved until the formatter side
         * is understood better.
         */
            bit = PRINT_FIELD_UNDOCUMENTED_L;
            break;
        case 'M':
            bit = PRINT_FIELD_SYMBOLIC_MODE;
            break;
        case 'G':
            bit = PRINT_FIELD_GROUP_NAME;
            break;
        case 'T':
            bit = PRINT_FIELD_FORMATTED_MTIME;
            break;
        case 'S':
            bit = PRINT_FIELD_FORMATTED_SIZE;
            break;
        case 'U':
            bit = PRINT_FIELD_USER_NAME;
            break;
        case 'c':
            bit = PRINT_FIELD_CHECKSUM;
            break;
        case 'g':
            bit = PRINT_FIELD_GROUP_ID;
            break;
        case 'm':
            bit = PRINT_FIELD_MODE;
            break;
        case 's':
            bit = PRINT_FIELD_SIZE;
            break;
        case 't':
            bit = PRINT_FIELD_MTIME;
            break;
        case 'u':
            bit = PRINT_FIELD_USER_ID;
            break;
        case '/':
            bit = PRINT_FIELD_UID_GID;
            break;
        case '?':
            bit = PRINT_FIELD_USER_GROUP_NAME;
            break;
        default:
            fprintf(stderr, "Unknown print option '%c'\n", *p);
            fflush(stderr);
            return false;
        }

        if (!mark_print_field(&mask, bit)) {
            return false;
        }
    }

    *mask_out = mask;
    return true;
}

static const char *tag_string_lookup(const struct tag_string_table *table, uint32_t tag) {
    if (!table || tag >= table->count) {
        return NULL;
    }

    return table->entries[tag];
}

static bool load_tag_string_table(struct tag_string_table **out_table, BOMStorage storage, const char *tree_name) {
    BOMTree tree = NULL;
    BOMTreeIterator iterator = NULL;
    struct tag_string_table *table = NULL;

    *out_table = NULL;

    tree = BOMTreeOpenWithName(storage, tree_name, false);
    if (!tree) {
        return false;
    }

    table = calloc(1, sizeof(*table));
    if (!table) {
        BOMTreeFree(tree);
        return false;
    }

    table->count = (uint32_t)BOMTreeCount(tree);
    table->entries = calloc(table->count ? table->count : 1u, sizeof(*table->entries));
    if (!table->entries) {
        free(table);
        BOMTreeFree(tree);
        return false;
    }

    iterator = BOMTreeIteratorNew(tree, NULL, NULL, NULL);
    while (iterator && !BOMTreeIteratorIsAtEnd(iterator)) {
        const char *key = (const char *)BOMTreeIteratorKey(iterator);
        const uint32_t *value_ptr = (const uint32_t *)BOMTreeIteratorValue(iterator);
        uint32_t index = value_ptr ? ntohl(*value_ptr) : 0;

        if (key && index < table->count) {
            table->entries[index] = strdup(key);
        }

        BOMTreeIteratorNext(iterator);
    }

    if (iterator) {
        BOMTreeIteratorFree(iterator);
    }

    BOMTreeFree(tree);
    *out_table = table;
    return true;
}

static void free_tag_string_table(struct tag_string_table *table) {
    if (!table) {
        return;
    }

    for (uint32_t i = 0; i < table->count; ++i) {
        free(table->entries[i]);
    }

    free(table->entries);
    free(table);
}

static bool alloc_tag_count_table(struct tag_count_table **out_table, uint32_t count) {
    struct tag_count_table *table = calloc(1, sizeof(*table));

    if (!table) {
        *out_table = NULL;
        return false;
    }

    table->count = count;
    table->counts = calloc(count ? count : 1u, sizeof(*table->counts));
    if (!table->counts) {
        free(table);
        *out_table = NULL;
        return false;
    }

    *out_table = table;
    return true;
}

static void free_tag_count_table(struct tag_count_table *table) {
    if (!table) {
        return;
    }

    free(table->counts);
    free(table);
}

static void warn_on_duplicate_tags(
    const struct tag_count_table *counts,
    BOMFSObject object,
    const char *kind,
    const struct tag_string_table *tags,
    uint32_t total_occurrences) {
    const char *path = NULL;

    if (!counts || !counts->counts) {
        return;
    }

    path = BOMFSObjectPathName(object);
    for (uint32_t i = 0; i < counts->count; ++i) {
        const uint32_t seen = counts->counts[i];

        if (seen < 2) {
            continue;
        }

        fprintf(
            stderr,
            "WARNING! %s 0x%08x \"%s\" was included %u/%u times for path \"%s\"\n",
            kind,
            i,
            tag_string_lookup(tags, i),
            seen,
            total_occurrences,
            path);
        fprintf(
            stdout,
            "    WARNING! %s 0x%08x \"%s\" was included %u/%u times for path \"%s\"\n",
            kind,
            i,
            tag_string_lookup(tags, i),
            seen,
            total_occurrences,
            path);
    }
}

static bool object_matches_type_filter(const struct lsbom_options *options, BOMFSObject object, BOMFSOType object_type) {
    int mode = 0;

    if (!options->type_filters_enabled) {
        return true;
    }

    switch (object_type) {
    case BOMFSOTypeFile:
        return options->list_files;
    case BOMFSOTypeDirectory:
        return options->list_dirs;
    case BOMFSOTypeSymlink:
        return options->list_symlinks;
    case BOMFSOTypeDevice:
        mode = BOMFSObjectMode(object) & 0xF000;
        if (mode == S_IFBLK) {
            return options->list_block_devices;
        }
        if (mode == S_IFCHR) {
            return options->list_char_devices;
        }
        return false;
    default:
        return false;
    }
}

static bool object_matches_arch_filter(const struct lsbom_options *options, BOMFSObject object, BOMFSOType object_type) {
    if (!options->have_arch_filter) {
        return true;
    }

    if (object_type != BOMFSOTypeFile) {
        return true;
    }

    if (!BOMFSObjectIsBinaryObject(object)) {
        return true;
    }

    return BOMFSObjectContainsArchitecture(object, options->arch_cputype) != 0;
}

static cpu_type_t summary_arch_selector(const struct lsbom_options *options, BOMFSObject object) {
    if (!options->have_arch_filter) {
        return (cpu_type_t)-1;
    }

    if (!BOMFSObjectIsBinaryObject(object)) {
        return (cpu_type_t)-1;
    }

    return options->arch_cputype;
}

static void dump_unknown_opaque_payload(const void *payload, size_t payload_size, const char *prefix) {
    fputs(prefix, stdout);
    BOMMemoryDump(payload, payload_size, NULL);
    fputc('\n', stdout);
}

static void dump_metabom_for_object(BOMStorage storage, BOMFSObject object, struct metabom_cache *cache) {
    const size_t opaque_block_id_size = sizeof(BOMBlockID);
    const size_t metabom_header_size = 2u * sizeof(uint32_t);
    const uint8_t *opaque = (const uint8_t *)BOMFSObjectOpaqueData(object);
    size_t opaque_size = 0;
    BOMBlockID block_id = 0;
    size_t block_size = 0;
    uint8_t *block = NULL;
    uint32_t project_count = 0;
    uint32_t package_count = 0;
    uint32_t total_words = 0;
    size_t used_bytes = 0;
    size_t align_padding = 0;
    size_t trailing_bytes = 0;
    struct tag_count_table *project_counts = NULL;
    struct tag_count_table *package_counts = NULL;

    if (!opaque) {
        fputc('\n', stdout);
        return;
    }

    fputs("  Opaque data ", stdout);
    opaque_size = BOMFSObjectOpaqueDataSize(object);

    if (opaque_size != opaque_block_id_size) {
        dump_unknown_opaque_payload(opaque, opaque_size, "[Unknown] ");
        return;
    }

    block_id = (BOMBlockID)OSReadBigInt32(opaque, 0);
    block_size = BOMStorageSizeOfBlock(storage, block_id);
    if (block_size < metabom_header_size) {
        fprintf(stdout, "[Unknown (< %zu)] ", metabom_header_size);
        BOMMemoryDump(opaque, opaque_block_id_size, NULL);
        fputc('\n', stdout);
        return;
    }

    block = malloc(block_size);
    if (!block) {
        fprintf(stdout, "[Unknown (malloc failed)] ");
        BOMMemoryDump(opaque, opaque_block_id_size, NULL);
        fputc('\n', stdout);
        return;
    }

    if (BOMStorageCopyFromBlock(storage, block_id, block) != 0) {
        fputs("[Unknown (Bad Block)] ", stdout);
        BOMMemoryDump(opaque, opaque_block_id_size, NULL);
        fputc('\n', stdout);
        free(block);
        return;
    }

    project_count = OSReadBigInt32(block, 0);
    package_count = OSReadBigInt32(block, sizeof(uint32_t));
    total_words = project_count + package_count + 2u;
    used_bytes = (size_t)total_words * sizeof(uint32_t);
    align_padding = (total_words & 1u) ? 4u : 0u;
    trailing_bytes = block_size - used_bytes;
    if (block_size < used_bytes) {
        fprintf(
            stderr,
            "[Metabom (Corrupt: %u Project Tags / %u Package Tags)] (%lu counts + %lu project + %lu package + %lu/%lu padding = %lu bytes)\n",
            project_count,
            package_count,
            8ul,
            (unsigned long)(4u * project_count),
            (unsigned long)(4u * package_count),
            (unsigned long)align_padding,
            (unsigned long)trailing_bytes,
            (unsigned long)block_size);
        fprintf(
            stdout,
            "[Metabom (Corrupt: %u Project Tags / %u Package Tags)] (%lu counts + %lu project + %lu package + %lu/%lu padding = %lu bytes)\n",
            project_count,
            package_count,
            8ul,
            (unsigned long)(4u * project_count),
            (unsigned long)(4u * package_count),
            (unsigned long)align_padding,
            (unsigned long)trailing_bytes,
            (unsigned long)block_size);
        BOMMemoryDump(block, block_size, NULL);
        fputc('\n', stdout);
        free(block);
        return;
    }

    fprintf(
        stdout,
        "[Metabom (%u Project Tags / %u Package Tags)] (%lu counts + %lu project + %lu package + %lu/%lu padding = %lu bytes)\n",
        project_count,
        package_count,
        8ul,
        (unsigned long)(4u * project_count),
        (unsigned long)(4u * package_count),
        (unsigned long)align_padding,
        (unsigned long)trailing_bytes,
        (unsigned long)block_size);

    if (!cache->project_tags) {
        load_tag_string_table(&cache->project_tags, storage, "ProjectTagTable");
    }
    if (!cache->package_tags) {
        load_tag_string_table(&cache->package_tags, storage, "PackageTagTable");
    }
    if (cache->project_tags) {
        alloc_tag_count_table(&project_counts, cache->project_tags->count);
    }
    if (cache->package_tags) {
        alloc_tag_count_table(&package_counts, cache->package_tags->count);
    }

    for (uint32_t i = 0; i < project_count; ++i) {
        uint32_t tag = OSReadBigInt32(block, 8 + (size_t)i * sizeof(uint32_t));
        const char *name = tag_string_lookup(cache->project_tags, tag);

        if (project_counts && tag < project_counts->count) {
            project_counts->counts[tag]++;
        }

        if (name) {
            fprintf(stdout, "    Project: 0x%08x \"%s\"\n", tag, name);
        } else {
            fprintf(stdout, "    Project: 0x%08x\n", tag);
        }
    }

    warn_on_duplicate_tags(project_counts, object, "project", cache->project_tags, project_count);

    for (uint32_t i = 0; i < package_count; ++i) {
        const size_t offset = 8 + (size_t)(project_count + i) * sizeof(uint32_t);
        uint32_t tag = OSReadBigInt32(block, offset);
        const char *name = tag_string_lookup(cache->package_tags, tag);

        if (package_counts && tag < package_counts->count) {
            package_counts->counts[tag]++;
        }

        if (name) {
            fprintf(stdout, "    Package: 0x%08x \"%s\"\n", tag, name);
        } else {
            fprintf(stdout, "    Package: 0x%08x\n", tag);
        }
    }

    warn_on_duplicate_tags(package_counts, object, "package", cache->package_tags, package_count);
    fputc('\n', stdout);

    free_tag_count_table(project_counts);
    free_tag_count_table(package_counts);
    free(block);
}

static void process_bom_path(const struct lsbom_options *options, const char *path) {
    BOMBom bom = NULL;
    BOMBomEnumerator enumerator = NULL;
    BOMStorage storage = NULL;
    BOMFSObject object = NULL;
    struct metabom_cache cache = {0};

    if (!BOMStorageIsStorageFileWithSys(path, NULL) || !(bom = BOMBomOpenWithSys(path, false, NULL))) {
        fprintf(stderr, "**** Can't open %s.\n", path);
        fflush(stderr);
        return;
    }

    enumerator = BOMBomEnumeratorNewWithOptions(bom, NULL, 1);
    if (!enumerator) {
        fprintf(stderr, "can't get enumerator for %s.\n", path);
        fflush(stderr);
        BOMBomFree(bom);
        return;
    }

    if (options->dump_bom_internals) {
        storage = BOMTreeStorage(BOMBomPathsTree(bom));
        BOMStorageDump(storage, BOMStorageDumpMemory);
        fwrite("\nBOM:\n", 6u, 1u, stdout);
    }

    while ((object = BOMBomEnumeratorNext(enumerator)) != NULL) {
        const BOMFSOType object_type = BOMFSObjectType(object);

        if (object_matches_type_filter(options, object, object_type) &&
            object_matches_arch_filter(options, object, object_type)) {
            if (options->paths_only) {
                puts(BOMFSObjectPathName(object));
            } else {
                const cpu_type_t arch_selector = summary_arch_selector(options, object);
                const char *summary = NULL;

                if (options->have_custom_format) {
                    summary = BOMFSObjectSummaryWithFormat(object, options->custom_format, arch_selector);
                } else {
                    summary = BOMFSObjectSummary(
                        object,
                        options->show_modified_times,
                        options->suppress_modes,
                        arch_selector);
                }

                printf("%s", summary);
            }

            if (storage) {
                dump_metabom_for_object(storage, object, &cache);
            }
        }

        BOMFSObjectFree(object);
    }

    free_tag_string_table(cache.project_tags);
    free_tag_string_table(cache.package_tags);
    BOMBomEnumeratorFree(enumerator);
    BOMBomFree(bom);
}

int main(int argc, char *argv[]) {
    struct lsbom_options options = {0};
    uint16_t print_mask = 0;
    int ch = 0;

    if (argc == 1) {
        usage(1);
    }

    normalize_legacy_long_options(argc, argv);

    while ((ch = getopt_long(argc, argv, "hbcdflmsxXp:", long_options, NULL)) != -1) {
        switch (ch) {
        case 'a': {
            const struct nx_arch_info_compat *info = NXGetArchInfoFromName(optarg);

            if (!info) {
                fprintf(stderr, "Unrecognized architecture %s\n", optarg);
                usage(0);
            }

            options.have_arch_filter = true;
            options.arch_cputype = info->cputype;
            break;
        }
        case 'b':
            options.list_block_devices = true;
            options.type_filters_enabled = true;
            break;
        case 'c':
            options.list_char_devices = true;
            options.type_filters_enabled = true;
            break;
        case 'd':
            options.list_dirs = true;
            options.type_filters_enabled = true;
            break;
        case 'f':
            options.list_files = true;
            options.type_filters_enabled = true;
            break;
        case 'h':
            usage(1);
            break;
        case 'l':
            options.list_symlinks = true;
            options.type_filters_enabled = true;
            break;
        case 'm':
            options.show_modified_times = true;
            break;
        case 'p':
            options.custom_format = optarg;
            options.have_custom_format = true;
            if (!validate_print_format(optarg, &print_mask)) {
                usage(0);
            }
            break;
        case 's':
            options.paths_only = true;
            break;
        case 'x':
            options.suppress_modes = true;
            break;
        case 'X':
            options.dump_bom_internals = true;
            break;
        default:
            usage(0);
        }
    }

    if (optind == argc) {
        usage(0);
    }

    for (int i = optind; i < argc; ++i) {
        process_bom_path(&options, argv[i]);
    }

    (void)print_mask;
    return 0;
}
