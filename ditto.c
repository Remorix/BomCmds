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
#if defined(__has_include)
#if __has_include(<readpassphrase.h>)
#include <readpassphrase.h>
#define HAVE_READPASSPHRASE_H 1
#endif
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_VARIANTS 16

#if !defined(HAVE_READPASSPHRASE_H)
extern char *readpassphrase(const char *prompt, char *buffer, size_t bufsiz, int flags);
#define RPP_REQUIRE_TTY 0x02
#endif

enum archive_mode {
    ARCHIVE_MODE_NONE = 0,
    ARCHIVE_MODE_CREATE,
    ARCHIVE_MODE_EXTRACT,
};

struct ditto_options {
    enum archive_mode archive_mode;
    bool cross_devices;
    bool use_gzip;
    bool use_bzip2;
    bool use_pkzip;
    bool request_pkzip_password;
    bool keep_parent;
    bool sequester_resources;
    bool copy_resources;
    bool copy_extattrs;
    bool copy_quarantine;
    bool copy_acls;
    bool use_hfs_compression;
    bool preserve_hfs_compression;
    bool use_filesystem_cache;
    bool nonatomic_copy;
    bool segment_large_files;
    bool persist_rootless;
    bool keep_binaries;
    bool test_options_only;
    const char *index_bom_path;
    const char *output_bom_path;
    const char *keep_binaries_list_path;
    const char *keep_binaries_pattern;
    const char *keep_binaries_dir;
    const char *zlib_compression_level;
    size_t arch_count;
    size_t lang_count;
    const char *architectures[MAX_VARIANTS + 1];
    const char *languages[MAX_VARIANTS + 1];
};

static volatile sig_atomic_t g_siginfo_requested = 0;
static int g_show_status = 0;
static int g_show_verbose_status = 0;
static bool g_saw_copy_error = false;
static long long g_total_bytes_copied = 0;
static const char *g_abort_crash_info = NULL;

static const struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"arch", required_argument, NULL, 'a'},
    {"lang", required_argument, NULL, 'l'},
    {"bom", required_argument, NULL, 'b'},
    {"outBom", required_argument, NULL, 'o'},
    {"keepBinaries", no_argument, NULL, 't'},
    {"keepBinariesList", required_argument, NULL, 'u'},
    {"keepBinariesPattern", required_argument, NULL, 'U'},
    {"keepParent", no_argument, NULL, 'p'},
    {"rsrc", no_argument, NULL, 'r'},
    {"norsrc", no_argument, NULL, 'R'},
    {"extattr", no_argument, NULL, 'n'},
    {"noextattr", no_argument, NULL, 'N'},
    {"qtn", no_argument, NULL, 'q'},
    {"noqtn", no_argument, NULL, 'Q'},
    {"acl", no_argument, NULL, 'm'},
    {"noacl", no_argument, NULL, 'M'},
    {"hfsCompression", no_argument, NULL, 'f'},
    {"nohfsCompression", no_argument, NULL, 'F'},
    {"preserveHFSCompression", no_argument, NULL, 'g'},
    {"nopreserveHFSCompression", no_argument, NULL, 'G'},
    {"zlibCompressionLevel", required_argument, NULL, 'i'},
    {"sequesterRsrc", no_argument, NULL, 's'},
    {"nocache", no_argument, NULL, 'y'},
    {"password", no_argument, NULL, 'W'},
    {"segmentLargeFiles", no_argument, NULL, 'L'},
    {"nonAtomicCopies", no_argument, NULL, 'I'},
    {"persistRootless", no_argument, NULL, 'e'},
    {"nopersistRootless", no_argument, NULL, 'E'},
    {"options", required_argument, NULL, 'O'},
    {NULL, 0, NULL, 0},
};

static const struct {
    const char *legacy;
    const char *canonical;
} legacy_long_option_rewrites[] = {
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

static void usage(int full) {
    fprintf(stderr, "Usage: %s [ <options> ] src [ ... src ] dst\n", getprogname());

    if (full == 1) {
        fputs(
            "\n"
            "    <options> are any of:\n"
            "    -h, --help                     print full usage\n"
            "    -v                             print a status line for each source copied\n"
            "    -V                             print a status line for every file copied\n"
            "    -X                             do not descend into directories with a\n"
            "                                   different device ID\n"
            "\n"
            "    -c                             create an archive at dst\n"
            "                                   The default format will be CPIO\n"
            "    -x                             src(s) are archives\n"
            "    -z                             Use gzip when compressing a CPIO archive\n"
            "    -j                             Use bzip2 when compressing a CPIO archive\n"
            "    -k                             create the archive in the PKZip format\n"
            "    --keepParent                   Embed the parent directory name of src in the\n"
            "                                   destination\n"
            "    --arch archVal                 fat files will be thinned to archVal\n"
            "                                   multiple --arch options can be specified\n"
            "                                   for example \"x86_86\", \"arm64\", etc\n"
            "    --bom bomFile                  only objects present in index bom are copied\n"
            "\n"
            "    --lang <lang>                  filter the index bom based on language\n"
            "                                   variants\n"
            "                                   multiple --lang options can be specified\n"
            "    --outBom bomFile               create an output bom representing the paths\n"
            "                                   that matched --arch or --lang variants\n"
            "\n"
            "    --rsrc                         preserve resource data\n"
            "    --norsrc                       do not preserve resource data\n"
            "    --extattr                      preserve extended attributes\n"
            "    --noextattr                    do not preserve extended attributes\n"
            "    --qtn                          preserve quarantine information\n"
            "    --noqtn                        do not preserve quarantine information\n"
            "    --acl                          preserve Access Control Lists (ACLs)\n"
            "    --noacl                        do not preserve ACLs\n"
            "\n"
            "    --sequesterRsrc                copy resources via polite __MACOSX directory\n"
            "                                   (this is for PKZip destinations only)\n"
            "    --zlibCompressionLevel <num>   use compression level 'num' when creating\n"
            "                                   a PKZip archive\n"
            "    --password                     request password for reading from encrypted\n"
            "                                   PKZip archive\n"
            "\n"
            "    --hfsCompression               compress files at destination if appropriate\n"
            "    --nohfsCompression             do not compress files at destination\n"
            "    --preserveHFSCompression       preserve filesystem compression when copying\n"
            "                                   files to HFS+ or APFS volumes\n"
            "    --nopreserveHFSCompression     do not preserve filesystem compression\n"
            "\n"
            "    --nocache                      do not use filesystem cache for reads/writes\n"
            "    --nonAtomicCopies              do not perform atomic copies when replacing\n"
            "                                   existing files\n"
            "    --segmentLargeFiles            when copying files to a CPIO archive, segment\n"
            "                                   files larger than 8GB into multiple entries\n"
            "                                   when copying files from a CPIO archive\n"
            "                                   reassemble segmented entries into a lone file\n"
            "\n"
            "    --persistRootless              persist the SF_RESTRICTED flag or the\n"
            "                                   com.apple.rootless extended attribute\n"
            "    --nopersistRootless            do not persist the SF_RESTRICTED flag or\n"
            "                                   com.apple.rootless extended attribute\n"
            "\n"
            "    --keepBinaries                 set aside the original Mach-O binary when it\n"
            "                                   is replaced by the source\n"
            "    --keepBinariesList <path>      record kept binaries in a text file at the\n"
            "                                   specified path\n"
            "    --keepBinariesPattern <regex>  only keep files that match the specified\n"
            "                                   regular expression\n"
            "\n"
            "Environment Variables:\n"
            "\n"
            "    DITTOABORT                     Abort if the copier encounters a fatal error\n"
            "\n"
            "    DITTONORSRC                    Do not preserve any filesystem metadata\n"
            "\n"
            "    DITTOKEEPBINARIESPATTERN       This is the equivalent of -U\n"
            "\n"
            "    DITTOKEEPBINARIESDIR           Specify an alternate directory for kept files\n"
            "\n"
            "    DITTO_TEST_OPTIONS             Print the BOMCopier parameters and exit\n"
            "\n"
            "Details on these options and their behaviors can be found in the ditto(1) manual\n",
            stderr);
    }

    exit(1);
}

static void abort_with_crash_info(const char *message) {
    g_abort_crash_info = message;
    abort();
}

static void handle_siginfo(int signo) {
    (void)signo;
    g_siginfo_requested = 1;
}

static void normalize_legacy_long_options(int argc, char **argv) {
    size_t j = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i - 1], "--") == 0) {
            continue;
        }

        for (j = 0; j < sizeof(legacy_long_option_rewrites) / sizeof(legacy_long_option_rewrites[0]); ++j) {
            if (strcmp(argv[i], legacy_long_option_rewrites[j].legacy) == 0) {
                argv[i] = (char *)legacy_long_option_rewrites[j].canonical;
                break;
            }
        }
    }
}

static int request_pkzip_password(BOMCopier copier, const char *path, char **password_out) {
    char prompt[4096];
    char stack_password[64];
    size_t password_length = 0;
    char *heap_password = NULL;

    (void)copier;

    if (snprintf(prompt, sizeof(prompt), "Please enter the password for file %s: ", path) > (int)sizeof(prompt)) {
        errx(2, "prompt buffer too small");
    }

    if (!readpassphrase(prompt, stack_password, sizeof(stack_password), RPP_REQUIRE_TTY)) {
        warnx("no password provided for encrypted archive - skipping file");
        return 1;
    }

    password_length = strlen(stack_password);
    heap_password = (char *)malloc(password_length + 1);
    if (!heap_password) {
        errx(2, "out of memory allocating password buffer");
    }

    strcpy(heap_password, stack_password);
    *password_out = heap_password;
    return 0;
}

static int verbose_copy_file_started(BOMCopier copier, const char *path, BOMCopierObjectType object_type) {
    (void)copier;

    if (g_siginfo_requested == 1) {
        fprintf(stderr, "%s\t%lld total bytes copied\n", path, g_total_bytes_copied);
        g_siginfo_requested = 0;
    }

    switch (object_type) {
    case BOMCopierObjectTypeFile:
        fprintf(stderr, "copying file %s ... \n", path);
        break;
    case BOMCopierObjectTypeSymlink:
        fprintf(stderr, "copying symlink %s ... \n", path);
        break;
    case BOMCopierObjectTypeDevice:
        fprintf(stderr, "copying device %s ... \n", path);
        break;
    default:
        break;
    }

    fflush(stderr);
    return 0;
}

static int verbose_copy_file_finished(
    BOMCopier copier,
    const char *path,
    BOMCopierObjectType object_type,
    long long bytes_copied,
    BOMCopierDisposition disposition) {
    (void)copier;

    g_total_bytes_copied += bytes_copied;
    if (g_siginfo_requested == 1) {
        fprintf(stderr, "%s\t%lld total bytes copied\n", path, g_total_bytes_copied);
        g_siginfo_requested = 0;
    }

    if (disposition == BOMCopierDispositionSkipped) {
        fprintf(stderr, "skipped %s\n", path);
    } else if (disposition == BOMCopierDispositionHardLinked) {
        fprintf(stderr, "hardlinked %s\n", path);
    } else {
        switch (object_type) {
        case BOMCopierObjectTypeDevice:
            fprintf(stderr, "copied %s\n", path);
            break;
        case BOMCopierObjectTypeSymlink:
            fprintf(stderr, "linked %s\n", path);
            break;
        case BOMCopierObjectTypeFile:
            fprintf(stderr, "%llu bytes for %s\n", (unsigned long long)bytes_copied, path);
            break;
        default:
            break;
        }
    }

    fflush(stderr);
    return 0;
}

static int update_copy_progress(BOMCopier copier, const char *path, long long bytes_copied) {
    (void)copier;
    if (g_siginfo_requested == 1) {
        fprintf(stderr, "%s\t%lld total bytes copied\n", path, g_total_bytes_copied + bytes_copied);
        g_siginfo_requested = 0;
    }
    return 0;
}

static int status_copy_file_started(BOMCopier copier, const char *path, BOMCopierObjectType object_type) {
    (void)copier;
    (void)object_type;
    if (g_siginfo_requested == 1) {
        fprintf(stderr, "%s\t%lld total bytes copied\n", path, g_total_bytes_copied);
        g_siginfo_requested = 0;
    }
    return 0;
}

static int status_copy_file_finished(
    BOMCopier copier,
    const char *path,
    BOMCopierObjectType object_type,
    long long bytes_copied,
    BOMCopierDisposition disposition) {
    (void)copier;
    (void)object_type;
    (void)disposition;

    g_total_bytes_copied += bytes_copied;
    if (g_siginfo_requested == 1) {
        fprintf(stderr, "%s\t%lld total bytes copied\n", path, g_total_bytes_copied);
        g_siginfo_requested = 0;
    }
    return 0;
}

static void copy_cf_dictionary_entry(const void *key, const void *value, void *context) {
    CFDictionarySetValue((CFMutableDictionaryRef)context, key, value);
}

static size_t fprint_cf_value(FILE *stream, CFTypeRef cf);

static void fprint_cf_dictionary(FILE *stream, CFDictionaryRef dict, int indent) {
    CFIndex count = 0;
    const void **keys = NULL;
    CFMutableArrayRef sorted_keys = NULL;

    count = CFDictionaryGetCount(dict);
    if (count <= 0) {
        return;
    }

    keys = (const void **)calloc((size_t)count, sizeof(*keys));
    if (!keys) {
        fwrite("Could not create key list\n", 26, 1, stream);
        return;
    }

    CFDictionaryGetKeysAndValues(dict, keys, NULL);
    sorted_keys = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!sorted_keys) {
        fwrite("Could not create empty array\n", 29, 1, stream);
        free(keys);
        return;
    }

    for (CFIndex i = 0; i < count; ++i) {
        CFIndex insert_at = CFArrayGetCount(sorted_keys);

        if (CFGetTypeID(keys[i]) == CFStringGetTypeID()) {
            for (CFIndex j = 0; j < CFArrayGetCount(sorted_keys); ++j) {
                const void *existing = CFArrayGetValueAtIndex(sorted_keys, j);

                if (CFGetTypeID(existing) == CFStringGetTypeID() && CFStringCompare((CFStringRef)keys[i], (CFStringRef)existing, 0) < 0) {
                    insert_at = j;
                    break;
                }
            }
        }

        if (insert_at == CFArrayGetCount(sorted_keys)) {
            CFArrayAppendValue(sorted_keys, keys[i]);
        } else {
            CFArrayInsertValueAtIndex(sorted_keys, insert_at, keys[i]);
        }
    }

    for (CFIndex i = 0; i < count; ++i) {
        const void *key = CFArrayGetValueAtIndex(sorted_keys, i);
        const void *value = CFDictionaryGetValue(dict, key);
        char key_buffer[255];
        CFTypeID value_type = 0;

        if (indent != 0) {
            for (int j = 0; j < indent; ++j) {
                fputc(' ', stream);
            }
        }

        if (CFGetTypeID(key) == CFStringGetTypeID() && CFStringGetCString((CFStringRef)key, key_buffer, sizeof(key_buffer), kCFStringEncodingUTF8)) {
            fprintf(stream, "%s: ", key_buffer);
        } else {
            fwrite("??: ", 4, 1, stream);
        }

        value_type = CFGetTypeID(value);
        if (value_type == CFArrayGetTypeID()) {
            CFArrayRef array = (CFArrayRef)value;
            CFIndex last_index = CFArrayGetCount(array) - 1;

            fputc('[', stream);
            for (CFIndex j = 0; j <= last_index; ++j) {
                fprint_cf_value(stream, (CFTypeRef)CFArrayGetValueAtIndex(array, j));
                if (j < last_index) {
                    fwrite(", ", 2, 1, stream);
                }
            }
            fwrite("]\n", 2, 1, stream);
            continue;
        }

        if (value_type == CFDictionaryGetTypeID()) {
            fwrite("{\n", 2, 1, stream);
            fprint_cf_dictionary(stream, (CFDictionaryRef)value, indent + 1);
            if (indent != 0) {
                for (int j = 0; j < indent; ++j) {
                    fputc(' ', stream);
                }
            }
            fwrite("}\n", 2, 1, stream);
            continue;
        }

        fprint_cf_value(stream, (CFTypeRef)value);
        fputc('\n', stream);
    }

    CFRelease(sorted_keys);
    free(keys);
}

static size_t fprint_cf_value(FILE *stream, CFTypeRef cf) {
    CFTypeID type = CFGetTypeID(cf);
    char buffer[256];
    CFNumberType number_type = 0;

    if (type == CFStringGetTypeID()) {
        CFStringGetCString((CFStringRef)cf, buffer, 255, kCFStringEncodingUTF8);
        return (size_t)fputs(buffer, stream);
    }

    if (type == CFBooleanGetTypeID()) {
        if (CFBooleanGetValue((CFBooleanRef)cf)) {
            return fwrite("true", 4, 1, stream);
        }
        return fwrite("false", 5, 1, stream);
    }

    if (type != CFNumberGetTypeID()) {
        if (type == CFArrayGetTypeID()) {
            return fwrite("[...]", 5, 1, stream);
        }
        if (type == CFDictionaryGetTypeID()) {
            return fwrite("{...}", 5, 1, stream);
        }
        return (size_t)type;
    }

    number_type = CFNumberGetType((CFNumberRef)cf);
    if (number_type == kCFNumberSInt64Type) {
        int64_t value = -1;

        if (CFNumberGetValue((CFNumberRef)cf, kCFNumberSInt64Type, &value)) {
            return (size_t)fprintf(stream, "%lld", (long long)value);
        }
        return 0;
    }

    if (number_type == kCFNumberSInt32Type) {
        int32_t value = -1;

        if (CFNumberGetValue((CFNumberRef)cf, kCFNumberSInt32Type, &value)) {
            return (size_t)fprintf(stream, "%d", value);
        }
        return 0;
    }

    return (size_t)fprintf(stream, "<Unsupported CFNumberType: %ld>", (long)number_type);
}

static void *fatal_error_handler(BOMCopier copier, const char *message) {
    char *abort_env = NULL;

    (void)copier;

    g_saw_copy_error = true;
    warnx("%s", message);

    abort_env = getenv("DITTOABORT");
    if (abort_env) {
        abort_with_crash_info(message);
    }

    return abort_env;
}

static void *fatal_file_error_handler(BOMCopier copier, const char *path, int errnum) {
    int effective_errno = errnum;
    char *abort_env = NULL;

    (void)copier;

    g_saw_copy_error = true;
    if (effective_errno == -1) {
        effective_errno = errno;
    }

    warnx("%s: %s", path, strerror(effective_errno));

    abort_env = getenv("DITTOABORT");
    if (abort_env) {
        char *message = NULL;

        if (asprintf(&message, "error code %d for %s", effective_errno, path) >= 0 && message) {
            abort_with_crash_info(message);
        }
        abort();
    }

    return abort_env;
}

static int file_error_handler(BOMCopier copier, const char *path, int errnum) {
    (void)copier;
    warnx("%s: %s", path, strerror(errnum));
    return 0;
}

int main(int argc, char **argv) {
    struct ditto_options options;
    CFMutableDictionaryRef direct_options = NULL;
    CFMutableDictionaryRef copier_options = NULL;
    BOMCopier copier = NULL;
    BOMBom source_bom = NULL;
    BOMBom filtered_bom = NULL;
    const char *effective_bom_path = NULL;
    char *temporary_bom_path = NULL;
    int option = 0;
    int failures = 0;
    int src_count = 0;
    char **src_argv = NULL;
    char *test_options_env = NULL;

    if (argc == 1) {
        usage(1);
    }

    memset(&options, 0, sizeof(options));
    options.cross_devices = true;
    options.copy_resources = getenv("DITTONORSRC") == NULL;
    options.copy_extattrs = getenv("DITTONORSRC") == NULL;
    options.copy_quarantine = getenv("DITTONORSRC") == NULL;
    options.copy_acls = getenv("DITTONORSRC") == NULL;
    options.preserve_hfs_compression = true;
    options.persist_rootless = true;
    options.use_filesystem_cache = true;
    options.keep_binaries_pattern = getenv("DITTOKEEPBINARIESPATTERN");
    options.keep_binaries_dir = getenv("DITTOKEEPBINARIESDIR");
    options.keep_binaries = options.keep_binaries_pattern != NULL;
    normalize_legacy_long_options(argc, argv);

    direct_options = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!direct_options) {
        errx(1, "Could not create direct options dictionary");
    }

    for (;;) {
        option = getopt_long(argc, argv, "hcjxzkVvX", long_options, NULL);
        if (option == -1) {
            break;
        }

        switch (option) {
        case 'E':
            options.persist_rootless = false;
            break;
        case 'F':
            options.use_hfs_compression = false;
            break;
        case 'G':
            options.preserve_hfs_compression = false;
            break;
        case 'I':
            options.nonatomic_copy = true;
            break;
        case 'L':
            options.segment_large_files = true;
            break;
        case 'M':
            options.copy_acls = false;
            break;
        case 'N':
            options.copy_extattrs = false;
            break;
        case 'O': {
            char *copy = strdup(optarg);
            char *separator = NULL;
            CFStringRef key = NULL;

            if (!copy) {
                err(1, "strdup");
            }

            separator = strchr(copy, '=');
            if (!separator) {
                fprintf(stderr, "I expect options in the form of key=value: %s\n", optarg);
                free(copy);
                return 1;
            }

            *separator = '\0';
            key = CFStringCreateWithCString(kCFAllocatorDefault, copy, kCFStringEncodingUTF8);
            if (!key) {
                fprintf(stderr, "Could not convert %s to a CFStringRef\n", copy);
                free(copy);
                return 1;
            }

            {
                const char *raw_value = separator + 1;
                size_t raw_length = strlen(raw_value);
                size_t raw_index = 0;

                if (strcmp(raw_value, "true") == 0 || strcmp(raw_value, "yes") == 0) {
                    CFDictionarySetValue(direct_options, key, kCFBooleanTrue);
                } else if (strcmp(raw_value, "false") == 0 || strcmp(raw_value, "no") == 0) {
                    CFDictionarySetValue(direct_options, key, kCFBooleanFalse);
                } else {
                    while (raw_index < raw_length && raw_value[raw_index] >= '0' && raw_value[raw_index] <= '9') {
                        ++raw_index;
                    }

                    if (raw_length == 0 || raw_index == raw_length) {
                        long long numeric_value = atoll(raw_value);
                        CFNumberRef number = NULL;

                        if (numeric_value > INT32_MAX) {
                            number = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &numeric_value);
                        } else {
                            int int_value = (int)numeric_value;
                            number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &int_value);
                        }

                        if (!number) {
                            fprintf(stderr, "Could not convert %s to a CFNumberRef\n", raw_value);
                            CFRelease(key);
                            free(copy);
                            return 1;
                        }

                        CFDictionarySetValue(direct_options, key, number);
                        CFRelease(number);
                    } else {
                        CFStringRef value_string = CFStringCreateWithCString(kCFAllocatorDefault, raw_value, kCFStringEncodingUTF8);

                        if (!value_string) {
                            fprintf(stderr, "Could not convert %s to a CFStringRef\n", raw_value);
                            CFRelease(key);
                            free(copy);
                            return 1;
                        }

                        CFDictionarySetValue(direct_options, key, value_string);
                        CFRelease(value_string);
                    }
                }
            }

            CFRelease(key);
            free(copy);
        } break;
        case 'Q':
            options.copy_quarantine = false;
            break;
        case 'R':
            options.copy_resources = false;
            break;
        case 'U':
            options.keep_binaries = true;
            options.keep_binaries_pattern = optarg;
            break;
        case 'V':
            ++g_show_verbose_status;
            ++g_show_status;
            break;
        case 'W':
            options.request_pkzip_password = true;
            break;
        case 'X':
            options.cross_devices = false;
            break;
        case 'a':
            if (options.arch_count == MAX_VARIANTS) {
                fprintf(stderr, "ERROR - maximum number (%d) of archs exceeded\n", MAX_VARIANTS);
                usage(0);
            }
            options.architectures[options.arch_count++] = optarg;
            options.architectures[options.arch_count] = NULL;
            break;
        case 'b':
            if (options.index_bom_path) {
                usage(0);
            }
            options.index_bom_path = optarg;
            break;
        case 'c':
            options.archive_mode = ARCHIVE_MODE_CREATE;
            break;
        case 'e':
            options.persist_rootless = true;
            break;
        case 'f':
            options.use_hfs_compression = true;
            break;
        case 'g':
            options.preserve_hfs_compression = true;
            break;
        case 'h':
            usage(1);
            break;
        case 'i':
            options.zlib_compression_level = optarg;
            break;
        case 'j':
            options.use_bzip2 = true;
            break;
        case 'k':
            options.use_pkzip = true;
            break;
        case 'l':
            if (options.lang_count == MAX_VARIANTS) {
                fprintf(stderr, "ERROR - maximum number (%d) of languages exceeded\n", MAX_VARIANTS);
                usage(0);
            }
            options.languages[options.lang_count++] = optarg;
            options.languages[options.lang_count] = NULL;
            break;
        case 'm':
            options.copy_acls = true;
            break;
        case 'n':
            options.copy_extattrs = true;
            break;
        case 'o':
            if (options.output_bom_path) {
                usage(0);
            }
            options.output_bom_path = optarg;
            break;
        case 'p':
            options.keep_parent = true;
            break;
        case 'q':
            options.copy_quarantine = true;
            break;
        case 'r':
            options.copy_resources = true;
            break;
        case 's':
            options.sequester_resources = true;
            break;
        case 't':
            options.keep_binaries = true;
            break;
        case 'u':
            options.keep_binaries = true;
            options.keep_binaries_list_path = optarg;
            break;
        case 'v':
            ++g_show_status;
            break;
        case 'x':
            options.archive_mode = ARCHIVE_MODE_EXTRACT;
            break;
        case 'y':
            options.use_filesystem_cache = false;
            break;
        case 'z':
            options.use_gzip = true;
            break;
        default:
            usage(0);
            break;
        }
    }

    test_options_env = getenv("DITTO_TEST_OPTIONS");
    options.test_options_only = test_options_env && strcmp(test_options_env, "1") == 0;

    src_count = argc - optind - 1;
    src_argv = &argv[optind];

    if (src_count <= 0) {
        warnx("No destination");
        usage(0);
    }
    if (options.archive_mode == ARCHIVE_MODE_CREATE && src_count != 1) {
        warnx("Can't archive multiple sources");
        usage(0);
    }
    if (options.use_gzip && options.archive_mode == ARCHIVE_MODE_NONE) {
        warnx("-z requires -x or -c");
        usage(0);
    }
    if (options.use_bzip2 && options.archive_mode == ARCHIVE_MODE_NONE) {
        warnx("-j requires -x or -c");
        usage(0);
    }
    if (options.use_pkzip && options.archive_mode == ARCHIVE_MODE_NONE) {
        warnx("-k requires -x or -c");
        usage(0);
    }
    if (options.keep_parent && options.archive_mode != ARCHIVE_MODE_CREATE) {
        warnx("--keepParent only works with -c");
        usage(0);
    }
    if (options.use_gzip && options.use_pkzip) {
        warnx("-z is only for cpio archives");
        usage(0);
    }
    if (options.use_bzip2 && options.use_pkzip) {
        warnx("-j is only for cpio archives");
        usage(0);
    }
    if (options.sequester_resources && !options.use_pkzip) {
        warnx("--sequesterRsrc is only for PKZip archives");
        usage(0);
    }
    if (options.segment_large_files && options.archive_mode != ARCHIVE_MODE_CREATE && options.archive_mode != ARCHIVE_MODE_EXTRACT) {
        warnx("--segmentLargeFiles is only for cpio archives");
        usage(0);
    }

    signal(SIGINFO, handle_siginfo);
    if ((options.archive_mode != ARCHIVE_MODE_NONE) && signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        err(1, "couldn't install SIGPIPE handler");
    }

    if (options.index_bom_path) {
        struct stat st;

        if (stat(options.index_bom_path, &st) == -1) {
            err(1, "ERROR: Can't access %s", options.index_bom_path);
        }

        source_bom = BOMBomOpen(options.index_bom_path, NULL);
        if (!source_bom) {
            err(1, "ERROR: Can't open bom %s", options.index_bom_path);
        }

        if (!BOMBomGetRootFSObject(source_bom)) {
            errx(0, "WARNING: bom %s contains no files, nothing copied", options.index_bom_path);
        }

        if (!options.output_bom_path && options.arch_count == 0 && options.lang_count == 0) {
            effective_bom_path = options.index_bom_path;
            BOMBomFree(source_bom);
            source_bom = NULL;
        } else {
            if (options.output_bom_path) {
                effective_bom_path = options.output_bom_path;
            } else {
                char temp_path[1024];
                int temporary_fd = -1;

                strlcpy(temp_path, "/tmp/ditto.XXXXX", sizeof(temp_path));
                temporary_fd = mkstemp(temp_path);
                if (temporary_fd == -1) {
                    err(1, "Could not mkstemp %s", temp_path);
                }

                close(temporary_fd);
                unlink(temp_path);
                strlcat(temp_path, ".bom", sizeof(temp_path));
                temporary_bom_path = strdup(temp_path);
                if (!temporary_bom_path) {
                    err(1, "strdup");
                }

                effective_bom_path = temporary_bom_path;
            }

            unlink(effective_bom_path);

            if (g_show_status) {
                const char *prefix = g_show_verbose_status ? ">>> " : "";

                fprintf(stderr, "%sCreating stripped mask bom %s... ", prefix, effective_bom_path);
            }

            filtered_bom = BOMBomNewFromBomWithOptions(
                effective_bom_path,
                source_bom,
                BOMBomFilterByArchAndLanguage,
                options.arch_count ? options.architectures : NULL,
                options.lang_count ? options.languages : NULL);
            if (!filtered_bom) {
                err(1, "Can't open bom %s", effective_bom_path);
            }

            if (!BOMBomGetRootFSObject(filtered_bom)) {
                errx(0, "WARNING: bom %s contains no files, no files copied", effective_bom_path);
            }

            if (g_show_status) {
                fwrite("done\n", 5, 1, stderr);
            }

            BOMBomFree(filtered_bom);
            filtered_bom = NULL;
            BOMBomFree(source_bom);
            source_bom = NULL;
        }
    }

    copier_options = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!copier_options) {
        errx(1, "couldn't create options dictionary");
    }

    if (effective_bom_path) {
        CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, effective_bom_path, kCFStringEncodingUTF8);

        if (!string) {
            errx(1, "could not create string from %s", effective_bom_path);
        }
        CFDictionarySetValue(copier_options, CFSTR("indexbom"), string);
        CFRelease(string);
    }

    if (options.arch_count != 0) {
        CFMutableArrayRef archs = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);

        if (!archs) {
            errx(1, "Could not create empty array");
        }

        for (size_t i = 0; i < options.arch_count; ++i) {
            CFStringRef entry = CFStringCreateWithCString(kCFAllocatorDefault, options.architectures[i], kCFStringEncodingUTF8);

            if (!entry) {
                errx(1, "could not create string from %s", options.architectures[i]);
            }
            CFArrayAppendValue(archs, entry);
            CFRelease(entry);
        }

        CFDictionarySetValue(copier_options, CFSTR("archs"), archs);
        CFRelease(archs);
    }

    if (options.copy_resources) {
        CFDictionarySetValue(copier_options, CFSTR("copyResources"), kCFBooleanTrue);
    }
    if (options.copy_extattrs) {
        CFDictionarySetValue(copier_options, CFSTR("copyExtendedAttributes"), kCFBooleanTrue);
    }
    if (options.copy_quarantine) {
        CFDictionarySetValue(copier_options, CFSTR("copyQuarantine"), kCFBooleanTrue);
    }
    if (options.copy_acls) {
        CFDictionarySetValue(copier_options, CFSTR("copyACLs"), kCFBooleanTrue);
    }

    if (options.use_hfs_compression) {
        CFDictionarySetValue(copier_options, CFSTR("useHFSPlusCompression"), kCFBooleanTrue);
    }
    if (options.preserve_hfs_compression) {
        CFDictionarySetValue(copier_options, CFSTR("preserveHFSPlusCompression"), kCFBooleanTrue);
    }
    if (options.keep_binaries) {
        CFDictionarySetValue(copier_options, CFSTR("keepBinaries"), kCFBooleanTrue);
    }
    if (options.keep_binaries_list_path) {
        CFStringRef string = CFStringCreateWithCString(
            kCFAllocatorDefault,
            options.keep_binaries_list_path,
            kCFStringEncodingUTF8);

        if (!string) {
            errx(1, "could not create string from %s", options.keep_binaries_list_path);
        }
        unlink(options.keep_binaries_list_path);
        CFDictionarySetValue(copier_options, CFSTR("keepBinariesList"), string);
        CFRelease(string);
    }
    if (options.keep_binaries_pattern) {
        CFStringRef string = CFStringCreateWithCString(
            kCFAllocatorDefault,
            options.keep_binaries_pattern,
            kCFStringEncodingUTF8);

        if (!string) {
            errx(1, "could not create string from %s", options.keep_binaries_pattern);
        }
        CFDictionarySetValue(copier_options, CFSTR("keepBinariesPattern"), string);
        CFRelease(string);
    }
    if (options.keep_binaries_dir) {
        CFStringRef string = CFStringCreateWithCString(
            kCFAllocatorDefault,
            options.keep_binaries_dir,
            kCFStringEncodingUTF8);

        if (!string) {
            errx(1, "could not create string from %s", options.keep_binaries_dir);
        }
        CFDictionarySetValue(copier_options, CFSTR("keepBinariesDir"), string);
        CFRelease(string);
    }

    CFDictionarySetValue(copier_options, CFSTR("crossDevices"), options.cross_devices ? kCFBooleanTrue : kCFBooleanFalse);
    if (options.segment_large_files) {
        CFDictionarySetValue(copier_options, CFSTR("segmentLargeFiles"), kCFBooleanTrue);
    }
    if (options.nonatomic_copy) {
        CFDictionarySetValue(copier_options, CFSTR("nonatomicCopy"), kCFBooleanTrue);
    }

    if (options.archive_mode == ARCHIVE_MODE_CREATE) {
        if (options.use_pkzip) {
            CFDictionarySetValue(copier_options, CFSTR("createPKZip"), kCFBooleanTrue);
            if (options.zlib_compression_level) {
                char *endptr = NULL;
                int level = (int)strtoq(options.zlib_compression_level, &endptr, 0);
                CFNumberRef number = NULL;

                if (endptr && *endptr != '\0') {
                    errx(1, "Invalid zlib compression level argument: %s", options.zlib_compression_level);
                }
                if (level < 0 || level > 9) {
                    errx(1, "Invalid zlib compression level: %d", level);
                }

                number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &level);
                if (!number) {
                    errx(1, "Could not convert zlib compression level %d into CFNumber", level);
                }
                CFDictionarySetValue(copier_options, CFSTR("zlibCompressionLevel"), number);
                CFRelease(number);
            }
        } else {
            CFDictionarySetValue(copier_options, CFSTR("createCPIO"), kCFBooleanTrue);
            if (options.use_bzip2) {
                CFDictionarySetValue(copier_options, CFSTR("compressCPIO"), CFSTR("bzip2"));
            } else if (options.use_gzip) {
                CFDictionarySetValue(copier_options, CFSTR("compressCPIO"), kCFBooleanTrue);
            }
        }
    } else if (options.archive_mode == ARCHIVE_MODE_EXTRACT) {
        if (options.use_pkzip) {
            CFDictionarySetValue(copier_options, CFSTR("extractPKZip"), kCFBooleanTrue);
        } else {
            CFDictionarySetValue(copier_options, CFSTR("extractCPIO"), kCFBooleanTrue);
            if (options.use_bzip2) {
                CFDictionarySetValue(copier_options, CFSTR("compressCPIO"), CFSTR("bzip2"));
            } else {
                CFDictionarySetValue(copier_options, CFSTR("compressCPIO"), kCFBooleanTrue);
            }
        }
    }

    if (options.keep_parent) {
        CFDictionarySetValue(copier_options, CFSTR("keepParent"), kCFBooleanTrue);
    }
    if (options.sequester_resources) {
        CFDictionarySetValue(copier_options, CFSTR("sequesterResources"), kCFBooleanTrue);
    }
    if (options.persist_rootless) {
        CFDictionarySetValue(copier_options, CFSTR("persistRestrictedFlags"), kCFBooleanTrue);
        CFDictionarySetValue(copier_options, CFSTR("persistRootlessEAs"), kCFBooleanTrue);
    }
    if (!options.use_filesystem_cache) {
        CFDictionarySetValue(copier_options, CFSTR("useFilesystemCache"), kCFBooleanFalse);
    }

    CFDictionaryApplyFunction(direct_options, copy_cf_dictionary_entry, copier_options);

    copier = BOMCopierNew();
    if (!copier) {
        errx(1, "Couldn't create copier");
    }

    BOMCopierSetFatalErrorHandler(copier, fatal_error_handler);
    BOMCopierSetFatalFileErrorHandler(copier, fatal_file_error_handler);
    BOMCopierSetFileErrorHandler(copier, file_error_handler);
    if (options.request_pkzip_password) {
        BOMCopierSetPKZipPasswordRequester(copier, request_pkzip_password);
    }
    if (g_show_verbose_status) {
        BOMCopierSetCopyFileStartedHandler(copier, verbose_copy_file_started);
        BOMCopierSetCopyFileFinishedHandler(copier, verbose_copy_file_finished);
        BOMCopierSetCopyFileUpdateHandler(copier, update_copy_progress);
    } else {
        BOMCopierSetCopyFileStartedHandler(copier, status_copy_file_started);
        BOMCopierSetCopyFileFinishedHandler(copier, status_copy_file_finished);
        BOMCopierSetCopyFileUpdateHandler(copier, update_copy_progress);
    }

    for (int i = 0; i < src_count; ++i) {
        const char *src = src_argv[i];
        const char *dst = src_argv[src_count];
        const char *effective_src = src;
        const char *effective_dst = dst;

        if (g_show_status) {
            const char *line_prefix = (i == 0 || !g_show_verbose_status) ? "" : "\n";
            const char *status_prefix = g_show_verbose_status ? ">>> " : "";

            fprintf(stderr, "%s%sCopying %s \n", line_prefix, status_prefix, src);
            fflush(stderr);
        }

        if (options.archive_mode == ARCHIVE_MODE_CREATE && dst && strcmp(dst, "-") == 0) {
            int output_fd = STDOUT_FILENO;
            CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &output_fd);

            if (!number) {
                errx(1, "couldn't create a CFNumber");
            }
            CFDictionarySetValue(copier_options, CFSTR("outputFD"), number);
            CFRelease(number);
            effective_dst = NULL;
        } else {
            CFDictionaryRemoveValue(copier_options, CFSTR("outputFD"));
        }

        if (options.archive_mode == ARCHIVE_MODE_EXTRACT && src && strcmp(src, "-") == 0) {
            int input_fd = STDIN_FILENO;
            CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &input_fd);

            if (!number) {
                errx(1, "couldn't create a CFNumber");
            }
            CFDictionarySetValue(copier_options, CFSTR("inputFD"), number);
            CFRelease(number);
            effective_src = NULL;
        } else {
            CFDictionaryRemoveValue(copier_options, CFSTR("inputFD"));
        }

        if (options.test_options_only) {
            FILE *stream = NULL;
            char *buffer = NULL;
            size_t size = 0;

            stream = open_memstream(&buffer, &size);
            if (!stream) {
                err(1, "Could not open memstream");
            }

            fprint_cf_dictionary(stream, copier_options, 1);
            fclose(stream);
            fprintf(stdout, "%s\n", effective_src ? effective_src : "(null)");
            fprintf(stdout, "%s\n", effective_dst ? effective_dst : "(null)");
            fputs(buffer, stdout);
            free(buffer);
            continue;
        }

        if (BOMCopierCopyWithOptions(copier, effective_src, effective_dst, copier_options) != 0) {
            ++failures;
            if (g_saw_copy_error && options.archive_mode == ARCHIVE_MODE_CREATE && effective_dst) {
                unlink(effective_dst);
            }
        }
    }

    if (temporary_bom_path) {
        unlink(temporary_bom_path);
        free(temporary_bom_path);
    }

    BOMCopierFree(copier);
    CFRelease(copier_options);
    CFRelease(direct_options);
    return failures;
}
