#include "include/opl.h"
#include "include/art_tar.h"
#include <ps2sdkapi.h>

static ArtTarEntry *s_tarIndex = NULL;
static u32 s_tarCount = 0;
static int s_tarFd = -1;

static u64 parseOctal(const char *s, int len)
{
    u64 val = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\0' || c == ' ')
            break;
        if (c < '0' || c > '7')
            break;
        val = (val << 3) + (u64)(c - '0');
    }
    return val;
}

static int isZeroBlock(const unsigned char header[TAR_BLOCK_SIZE])
{
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (header[i] != 0)
            return 0;
    }
    return 1;
}

ArtTarEntry *findTarEntry(const char *filename)
{
    if (!filename || !s_tarIndex || s_tarCount == 0)
        return NULL;
    for (u32 i = 0; i < s_tarCount; i++) {
        if (strcasecmp(s_tarIndex[i].filename, filename) == 0) {
            return &s_tarIndex[i];
        }
    }
    return NULL;
}

int loadTarFile(const char *path)
{
    if (s_tarFd >= 0 || s_tarIndex)
        closeTarFile();

    s_tarFd = open(path, O_RDONLY);
    if (s_tarFd < 0)
        return -1;

    s_tarIndex = NULL;
    s_tarCount = 0;

    while (1) {
        unsigned char header[TAR_BLOCK_SIZE];
        int bytesRead = read(s_tarFd, header, TAR_BLOCK_SIZE);
        if (bytesRead == 0) {
            return 0; // EOF
        }
        if (bytesRead != TAR_BLOCK_SIZE) {
            closeTarFile();
            return -1;
        }

        if (isZeroBlock(header))
            return 0;

        char name[21];
        memcpy(name, header, 20);
        name[20] = '\0';

        u64 rawSize64 = parseOctal((const char *)header + 124, 12);
        u64 paddedSize64 = ((rawSize64 + (TAR_BLOCK_SIZE - 1)) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;

        if (rawSize64 > MAX_FILE_SIZE || paddedSize64 > MAX_FILE_SIZE) {
            closeTarFile();
            return -1;
        }

        u64 dataOffset = lseek64(s_tarFd, 0, SEEK_CUR);
        if (dataOffset == (u64)-1) {
            closeTarFile();
            return -1;
        }

        ArtTarEntry *newIndex = realloc(s_tarIndex, sizeof(ArtTarEntry) * (s_tarCount + 1));
        if (!newIndex) {
            closeTarFile();
            return -1;
        }
        s_tarIndex = newIndex;

        ArtTarEntry *entry = &s_tarIndex[s_tarCount];
        strncpy(entry->filename, name, 20);
        entry->filename[20] = '\0';
        entry->offset = dataOffset;
        entry->rawSize = (u32)rawSize64;
        entry->paddedSize = (u32)paddedSize64;
        s_tarCount++;

        if (lseek64(s_tarFd, paddedSize64, SEEK_CUR) == (u64)-1) {
            closeTarFile();
            return -1;
        }
    }
}

int closeTarFile(void)
{
    if (s_tarFd >= 0) {
        close(s_tarFd);
        s_tarFd = -1;
    }
    free(s_tarIndex);
    s_tarIndex = NULL;
    s_tarCount = 0;
    return 0;
}

int hasTarEntry(const char *filename)
{
    return findTarEntry(filename) ? 1 : 0;
}

u32 readFileFromTar(const ArtTarEntry *entry, void *dst, u32 dstSize)
{
    if (!entry || s_tarFd < 0 || !dst)
        return 0;
    if (dstSize < entry->rawSize)
        return 0;

    if (lseek64(s_tarFd, entry->offset, SEEK_SET) != entry->offset)
        return 0;

    u32 total = 0;
    while (total < entry->rawSize) {
        int bytesRead = read(s_tarFd, (unsigned char *)dst + total, entry->rawSize - total);
        if (bytesRead <= 0)
            return 0;

        total += (u32)bytesRead;
    }
    return total;
}

void *getFileFromTar(const char *filename)
{
    ArtTarEntry *entry = findTarEntry(filename);
    if (!entry)
        return NULL;

    void *buffer = malloc(entry->rawSize);
    if (!buffer)
        return NULL;

    u32 bytesRead = readFileFromTar(entry, buffer, entry->rawSize);
    if (bytesRead == 0 || bytesRead != entry->rawSize) {
        free(buffer);
        return NULL;
    }
    return buffer;
}