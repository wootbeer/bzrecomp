// Controller Pak (memory card) saves.
//
// Games that save to a Controller Pak do it through libultra's osPfs* file
// API. TODO(bz): confirm Battlezone: Rise of the Black Dogs saves this way
// (and has no cartridge EEPROM/SRAM/Flash save; see .save_type in
// src/main/main.cpp). N64Recomp
// replaces those functions with *_recomp versions, which stock librecomp
// (librecomp/src/pak.cpp) implements as "no pak inserted". These
// definitions take their place (with all ten defined here, the linker never
// pulls pak.cpp's object out of librecomp) and back each controller's pak
// with a raw 32 KB image file in the saves folder -- the standard .mpk
// layout Project64 and other emulators use, so saves can be copied
// between them:
//
//   page 0       ID area (label block, then the ID block and its backups)
//   pages 1, 2   inode table and its backup: one u16 per page, holding the
//                next page of the file, PFS_EOF or PFS_PAGE_NOT_USED.
//                Entry 0's low byte is a checksum of entries 5-127.
//   pages 3, 4   note table: 16 directory entries of 32 bytes
//   pages 5-127  file data (123 pages, the usual "123 free pages")
//
// Which controllers hold a Controller Pak rather than a Rumble Pak comes
// from the General tab (src/main/game_config.cpp). libultra games commonly
// probe each controller with osPfsInitPak and fall back to osMotorInit when
// that returns PFS_ERR_ID_FATAL, so a Rumble Pak port answers with exactly
// that. TODO(bz): check how this game probes its paks.
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

#include "ultramodern/ultramodern.hpp"
#include "librecomp/files.hpp"
#include "recomp.h"

#include "bz_config.h"

namespace {
    constexpr int kPageSize = 256;
    constexpr int kPages = 128;
    constexpr int kPakSize = kPageSize * kPages;
    constexpr int kBlockSize = 32;
    constexpr int kInodePage = 1;
    constexpr int kInodeBackupPage = 2;
    constexpr int kDirPage = 3;
    constexpr int kFirstDataPage = 5;
    constexpr int kDirEntries = 16;
    constexpr int kDirEntrySize = 32;
    constexpr int kMaxPorts = 4;

    constexpr uint16_t kInodeEof = 1;     // PFS_EOF
    constexpr uint16_t kInodeFree = 3;    // PFS_PAGE_NOT_USED
    constexpr uint8_t kDirOccupied = 2;   // DIR_STATUS_OCCUPIED

    constexpr int32_t kPfsInitialized = 1; // PFS_INITIALIZED

    enum PfsError : int32_t {
        PFS_ERR_NOPACK = 1,
        PFS_ERR_NEW_PACK = 2,
        PFS_ERR_INCONSISTENT = 3,
        PFS_ERR_CONTRFAIL = 4,
        PFS_ERR_INVALID = 5,
        PFS_ERR_BAD_DATA = 6,
        PFS_DATA_FULL = 7,
        PFS_DIR_FULL = 8,
        PFS_ERR_EXIST = 9,
        PFS_ERR_ID_FATAL = 10,
        PFS_ERR_DEVICE = 11,
    };

    // OSPfs field offsets, as the game sees the struct.
    constexpr int kPfsStatus = 0x00;
    constexpr int kPfsQueue = 0x04;
    constexpr int kPfsChannel = 0x08;
    constexpr int kPfsId = 0x0C;
    constexpr int kPfsLabel = 0x2C;
    constexpr int kPfsVersion = 0x4C;
    constexpr int kPfsDirSize = 0x50;
    constexpr int kPfsInodeTable = 0x54;
    constexpr int kPfsMinodeTable = 0x58;
    constexpr int kPfsDirTable = 0x5C;
    constexpr int kPfsInodeStartPage = 0x60;
    constexpr int kPfsBanks = 0x64;
    constexpr int kPfsActiveBank = 0x65;

    struct DirEntry {
        uint32_t game_code;
        uint16_t company_code;
        uint16_t start_page;
        uint8_t status;
        uint8_t ext_name[4];
        uint8_t game_name[16];

        bool empty() const { return game_code == 0 || company_code == 0; }
    };

    struct Pak {
        std::array<uint8_t, kPakSize> data{};

        uint16_t read16(int offset) const { return (uint16_t)((data[offset] << 8) | data[offset + 1]); }
        void write16(int offset, uint16_t value) {
            data[offset] = (uint8_t)(value >> 8);
            data[offset + 1] = (uint8_t)value;
        }

        uint16_t inode(int page) const { return read16(kInodePage * kPageSize + page * 2); }
        void set_inode(int page, uint16_t value) { write16(kInodePage * kPageSize + page * 2, value); }

        static uint8_t inode_checksum(const uint8_t* table) {
            uint8_t sum = 0;
            for (int i = kFirstDataPage * 2; i < kPageSize; i++) {
                sum += table[i];
            }
            return sum;
        }

        bool inode_table_valid(int page) const {
            const uint8_t* table = &data[page * kPageSize];
            return table[1] == inode_checksum(table);
        }

        // Re-checksums the inode table and mirrors it to the backup page.
        void commit_inodes() {
            uint8_t* table = &data[kInodePage * kPageSize];
            table[0] = 0;
            table[1] = inode_checksum(table);
            memcpy(&data[kInodeBackupPage * kPageSize], table, kPageSize);
        }

        DirEntry dir(int file_no) const {
            int o = kDirPage * kPageSize + file_no * kDirEntrySize;
            DirEntry e{};
            e.game_code = ((uint32_t)read16(o) << 16) | read16(o + 2);
            e.company_code = read16(o + 4);
            e.start_page = read16(o + 6);
            e.status = data[o + 8];
            memcpy(e.ext_name, &data[o + 12], 4);
            memcpy(e.game_name, &data[o + 16], 16);
            return e;
        }

        void set_dir(int file_no, const DirEntry& e) {
            int o = kDirPage * kPageSize + file_no * kDirEntrySize;
            memset(&data[o], 0, kDirEntrySize);
            write16(o, (uint16_t)(e.game_code >> 16));
            write16(o + 2, (uint16_t)e.game_code);
            write16(o + 4, e.company_code);
            write16(o + 6, e.start_page);
            data[o + 8] = e.status;
            memcpy(&data[o + 12], e.ext_name, 4);
            memcpy(&data[o + 16], e.game_name, 16);
        }

        int free_pages() const {
            int count = 0;
            for (int page = kFirstDataPage; page < kPages; page++) {
                count += inode(page) == kInodeFree;
            }
            return count;
        }

        // Pages of a file in order, or empty if its chain is broken.
        std::vector<int> chain(const DirEntry& e) const {
            std::vector<int> pages;
            uint16_t next = e.start_page;
            while (next != kInodeEof) {
                int page = next & 0xFF;
                if ((next >> 8) != 0 || page < kFirstDataPage || pages.size() >= kPages - kFirstDataPage) {
                    return {};
                }
                pages.push_back(page);
                next = inode(page);
            }
            return pages;
        }

        // A freshly formatted pak, as osPfsReFormat / emulators lay it out.
        void format() {
            data.fill(0);
            // Label block: the pattern N64 formatting tools write.
            data[0] = 0x81;
            for (int i = 1; i < kBlockSize; i++) {
                data[i] = (uint8_t)i;
            }
            // ID block (__OSPackId) at its primary and three backup locations.
            uint8_t id[kBlockSize] = {
                0xFF, 0xFF, 0xFF, 0xFF, // repaired
                0x42, 0x54, 0x47, 0x41, // random
                0, 0, 0, 0, 0, 0, 0, 0, // serial_mid
                0, 0, 0, 0, 0, 0, 0, 0, // serial_low
                0x00, 0x01,             // deviceid
                0x01,                   // banks
                0x00,                   // version
            };
            uint16_t sum = 0;
            for (int i = 0; i < 28; i += 2) {
                sum += (uint16_t)((id[i] << 8) | id[i + 1]);
            }
            uint16_t inverted = (uint16_t)(0xFFF2 - sum);
            id[28] = (uint8_t)(sum >> 8);
            id[29] = (uint8_t)sum;
            id[30] = (uint8_t)(inverted >> 8);
            id[31] = (uint8_t)inverted;
            for (int block : { 1, 3, 4, 6 }) {
                memcpy(&data[block * kBlockSize], id, kBlockSize);
            }
            for (int page = 0; page < kPages; page++) {
                set_inode(page, page < kFirstDataPage ? 0 : kInodeFree);
            }
            commit_inodes();
        }
    };

    std::mutex pak_mutex;
    std::array<Pak, kMaxPorts> paks;

    std::filesystem::path pak_path(int port) {
        std::filesystem::path save_path = ultramodern::get_save_file_path();
        std::u8string name = save_path.stem().u8string() + u8"_pak" + (char8_t)(u8'1' + port) + u8".mpk";
        return save_path.parent_path() / name;
    }

    void load_pak(int port) {
        Pak& pak = paks[port];
        std::filesystem::path path = pak_path(port);
        std::ifstream file = recomp::open_input_file_with_backup(path, std::ios_base::binary);
        if (!file.good()) {
            pak.format();
            return;
        }
        file.read(reinterpret_cast<char*>(pak.data.data()), kPakSize);
        if (file.gcount() != kPakSize) {
            fprintf(stderr, "Controller Pak file %s is shorter than 32 KB; using a freshly formatted pak instead.\n", path.string().c_str());
            pak.format();
            return;
        }
        if (!pak.inode_table_valid(kInodePage)) {
            if (pak.inode_table_valid(kInodeBackupPage)) {
                memcpy(&pak.data[kInodePage * kPageSize], &pak.data[kInodeBackupPage * kPageSize], kPageSize);
            } else {
                fprintf(stderr, "Controller Pak file %s has a bad inode checksum; repairing it.\n", path.string().c_str());
                pak.commit_inodes();
            }
        }
    }

    void save_pak(int port) {
        std::filesystem::path path = pak_path(port);
        std::filesystem::create_directories(path.parent_path());
        bool ok = false;
        {
            std::ofstream file = recomp::open_output_file_with_backup(path, std::ios_base::binary);
            if (file.good()) {
                file.write(reinterpret_cast<const char*>(paks[port].data.data()), kPakSize);
                ok = file.good();
            }
        }
        ok = ok && recomp::finalize_output_file_with_backup(path);
        if (!ok) {
            ultramodern::error_handling::message_box("Failed to write the Controller Pak save file. Check your file permissions and whether the save folder has been moved to Dropbox or similar, as this can cause issues.");
        }
    }

    gpr arg(recomp_context* ctx, int index) {
        return (&ctx->r4)[index];
    }

    gpr stack_arg(uint8_t* rdram, recomp_context* ctx, int index) {
        return (gpr)(int32_t)MEM_W(index * 4, ctx->r29);
    }

    // Validates the OSPfs a call was given and returns its port, or a PFS
    // error (negated) if there's no usable Controller Pak behind it.
    int pfs_port(uint8_t* rdram, gpr pfs) {
        if ((MEM_W(kPfsStatus, pfs) & kPfsInitialized) == 0) {
            return -PFS_ERR_INVALID;
        }
        int port = MEM_W(kPfsChannel, pfs);
        if (port < 0 || port >= kMaxPorts) {
            return -PFS_ERR_INVALID;
        }
        if (bz::config::get_accessory(port) != bz::config::Accessory::ControllerPak) {
            return -PFS_ERR_NOPACK;
        }
        return port;
    }

    void read_name(uint8_t* rdram, gpr ptr, uint8_t* out, int length) {
        for (int i = 0; i < length; i++) {
            out[i] = (uint8_t)MEM_B(i, ptr);
        }
    }

    int find_file(const Pak& pak, uint16_t company_code, uint32_t game_code, const uint8_t* game_name, const uint8_t* ext_name) {
        for (int file_no = 0; file_no < kDirEntries; file_no++) {
            DirEntry e = pak.dir(file_no);
            if (!e.empty() && e.company_code == company_code && e.game_code == game_code &&
                memcmp(e.game_name, game_name, 16) == 0 && memcmp(e.ext_name, ext_name, 4) == 0) {
                return file_no;
            }
        }
        return -1;
    }

    void ret(recomp_context* ctx, int32_t value) {
        ctx->r2 = (gpr)value;
    }
}

// s32 osPfsInitPak(OSMesgQueue* mq, OSPfs* pfs, int channel)
extern "C" void osPfsInitPak_recomp(uint8_t* rdram, recomp_context* ctx) {
    gpr mq = arg(ctx, 0);
    gpr pfs = arg(ctx, 1);
    int port = (int)arg(ctx, 2);

    if (port < 0 || port >= kMaxPorts) {
        ret(ctx, PFS_ERR_CONTRFAIL);
        return;
    }
    switch (bz::config::get_accessory(port)) {
    case bz::config::Accessory::RumblePak:
        // What a real Rumble Pak's unreadable ID area produces; the game
        // tries osMotorInit next.
        ret(ctx, PFS_ERR_ID_FATAL);
        return;
    case bz::config::Accessory::None:
        ret(ctx, PFS_ERR_NOPACK);
        return;
    case bz::config::Accessory::ControllerPak:
        break;
    }

    std::lock_guard lock{ pak_mutex };
    // Re-read on every init, as the game does when it rescans the
    // controllers, so a swapped-in .mpk file is picked up like a new pak.
    load_pak(port);
    const Pak& pak = paks[port];

    MEM_W(kPfsStatus, pfs) = kPfsInitialized;
    MEM_W(kPfsQueue, pfs) = (int32_t)mq;
    MEM_W(kPfsChannel, pfs) = port;
    for (int i = 0; i < kBlockSize; i++) {
        MEM_B(kPfsId + i, pfs) = (int8_t)pak.data[kBlockSize + i];
        MEM_B(kPfsLabel + i, pfs) = (int8_t)pak.data[i];
    }
    MEM_W(kPfsVersion, pfs) = pak.data[kBlockSize + 27];
    MEM_W(kPfsDirSize, pfs) = kDirEntries;
    MEM_W(kPfsInodeTable, pfs) = kInodePage * (kPageSize / kBlockSize);
    MEM_W(kPfsMinodeTable, pfs) = kInodeBackupPage * (kPageSize / kBlockSize);
    MEM_W(kPfsDirTable, pfs) = kDirPage * (kPageSize / kBlockSize);
    MEM_W(kPfsInodeStartPage, pfs) = kFirstDataPage;
    MEM_B(kPfsBanks, pfs) = 1;
    MEM_B(kPfsActiveBank, pfs) = 0;

    ret(ctx, 0);
}

// s32 osPfsFreeBlocks(OSPfs* pfs, s32* bytes_not_used)
extern "C" void osPfsFreeBlocks_recomp(uint8_t* rdram, recomp_context* ctx) {
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }
    std::lock_guard lock{ pak_mutex };
    MEM_W(0, arg(ctx, 1)) = paks[port].free_pages() * kPageSize;
    ret(ctx, 0);
}

// s32 osPfsAllocateFile(OSPfs* pfs, u16 company_code, u32 game_code, u8* game_name,
//                       u8* ext_name, int file_size_in_bytes, s32* file_no)
extern "C" void osPfsAllocateFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint16_t company_code = (uint16_t)arg(ctx, 1);
    uint32_t game_code = (uint32_t)arg(ctx, 2);
    int32_t size = (int32_t)stack_arg(rdram, ctx, 5);
    gpr file_no_ptr = stack_arg(rdram, ctx, 6);

    if (company_code == 0 || game_code == 0 || size <= 0) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }

    DirEntry e{};
    e.game_code = game_code;
    e.company_code = company_code;
    e.status = kDirOccupied;
    read_name(rdram, arg(ctx, 3), e.game_name, 16);
    read_name(rdram, stack_arg(rdram, ctx, 4), e.ext_name, 4);

    std::lock_guard lock{ pak_mutex };
    Pak& pak = paks[port];
    if (find_file(pak, company_code, game_code, e.game_name, e.ext_name) >= 0) {
        ret(ctx, PFS_ERR_EXIST);
        return;
    }
    int file_no = -1;
    for (int i = 0; i < kDirEntries; i++) {
        DirEntry slot = pak.dir(i);
        if (slot.game_code == 0 && slot.company_code == 0) {
            file_no = i;
            break;
        }
    }
    if (file_no < 0) {
        ret(ctx, PFS_DIR_FULL);
        return;
    }
    int pages_needed = (size + kPageSize - 1) / kPageSize;
    if (pages_needed > pak.free_pages()) {
        ret(ctx, PFS_DATA_FULL);
        return;
    }

    int prev = -1;
    for (int page = kFirstDataPage; page < kPages && pages_needed > 0; page++) {
        if (pak.inode(page) != kInodeFree) {
            continue;
        }
        if (prev < 0) {
            e.start_page = (uint16_t)page;
        } else {
            pak.set_inode(prev, (uint16_t)page);
        }
        prev = page;
        pages_needed--;
    }
    pak.set_inode(prev, kInodeEof);
    pak.commit_inodes();
    pak.set_dir(file_no, e);
    save_pak(port);

    MEM_W(0, file_no_ptr) = file_no;
    ret(ctx, 0);
}

// s32 osPfsDeleteFile(OSPfs* pfs, u16 company_code, u32 game_code, u8* game_name, u8* ext_name)
extern "C" void osPfsDeleteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint16_t company_code = (uint16_t)arg(ctx, 1);
    uint32_t game_code = (uint32_t)arg(ctx, 2);
    if (company_code == 0 || game_code == 0) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }
    uint8_t game_name[16], ext_name[4];
    read_name(rdram, arg(ctx, 3), game_name, 16);
    read_name(rdram, stack_arg(rdram, ctx, 4), ext_name, 4);

    std::lock_guard lock{ pak_mutex };
    Pak& pak = paks[port];
    int file_no = find_file(pak, company_code, game_code, game_name, ext_name);
    if (file_no < 0) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }
    for (int page : pak.chain(pak.dir(file_no))) {
        pak.set_inode(page, kInodeFree);
    }
    pak.commit_inodes();
    pak.set_dir(file_no, DirEntry{});
    save_pak(port);
    ret(ctx, 0);
}

// s32 osPfsFileState(OSPfs* pfs, s32 file_no, OSPfsState* state)
extern "C" void osPfsFileState_recomp(uint8_t* rdram, recomp_context* ctx) {
    int32_t file_no = (int32_t)arg(ctx, 1);
    gpr state = arg(ctx, 2);
    if (file_no < 0 || file_no >= kDirEntries) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }

    std::lock_guard lock{ pak_mutex };
    const Pak& pak = paks[port];
    DirEntry e = pak.dir(file_no);
    if (e.empty()) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }
    std::vector<int> pages = pak.chain(e);
    if (pages.empty()) {
        ret(ctx, PFS_ERR_INCONSISTENT);
        return;
    }
    // OSPfsState: file_size, game_code, company_code, ext_name[4], game_name[16]
    MEM_W(0x00, state) = (int32_t)(pages.size() * kPageSize);
    MEM_W(0x04, state) = (int32_t)e.game_code;
    MEM_H(0x08, state) = (int16_t)e.company_code;
    for (int i = 0; i < 4; i++) {
        MEM_B(0x0A + i, state) = (int8_t)e.ext_name[i];
    }
    for (int i = 0; i < 16; i++) {
        MEM_B(0x0E + i, state) = (int8_t)e.game_name[i];
    }
    ret(ctx, 0);
}

// s32 osPfsFindFile(OSPfs* pfs, u16 company_code, u32 game_code, u8* game_name,
//                   u8* ext_name, s32* file_no)
extern "C" void osPfsFindFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint16_t company_code = (uint16_t)arg(ctx, 1);
    uint32_t game_code = (uint32_t)arg(ctx, 2);
    gpr file_no_ptr = stack_arg(rdram, ctx, 5);
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }
    uint8_t game_name[16], ext_name[4];
    read_name(rdram, arg(ctx, 3), game_name, 16);
    read_name(rdram, stack_arg(rdram, ctx, 4), ext_name, 4);

    std::lock_guard lock{ pak_mutex };
    int file_no = find_file(paks[port], company_code, game_code, game_name, ext_name);
    MEM_W(0, file_no_ptr) = file_no;
    ret(ctx, file_no >= 0 ? 0 : PFS_ERR_INVALID);
}

// s32 osPfsReadWriteFile(OSPfs* pfs, s32 file_no, u8 flag, int offset,
//                        int size_in_bytes, u8* data_buffer)
extern "C" void osPfsReadWriteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    int32_t file_no = (int32_t)arg(ctx, 1);
    bool write = (uint8_t)arg(ctx, 2) != 0; // PFS_WRITE
    int32_t offset = (int32_t)arg(ctx, 3);
    int32_t size = (int32_t)stack_arg(rdram, ctx, 4);
    gpr buffer = stack_arg(rdram, ctx, 5);

    if (file_no < 0 || file_no >= kDirEntries || size <= 0 || (size % kBlockSize) != 0 ||
        offset < 0 || (offset % kBlockSize) != 0) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }

    std::lock_guard lock{ pak_mutex };
    Pak& pak = paks[port];
    DirEntry e = pak.dir(file_no);
    if (e.empty()) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }
    std::vector<int> pages = pak.chain(e);
    if (pages.empty()) {
        ret(ctx, PFS_ERR_INCONSISTENT);
        return;
    }
    if ((size_t)offset + size > pages.size() * kPageSize) {
        ret(ctx, PFS_ERR_INVALID);
        return;
    }

    for (int32_t i = 0; i < size; i++) {
        int32_t file_offset = offset + i;
        uint8_t& byte = pak.data[pages[file_offset / kPageSize] * kPageSize + file_offset % kPageSize];
        if (write) {
            byte = (uint8_t)MEM_B(i, buffer);
        } else {
            MEM_B(i, buffer) = (int8_t)byte;
        }
    }
    if (write) {
        if (e.status != kDirOccupied) {
            e.status = kDirOccupied;
            pak.set_dir(file_no, e);
        }
        save_pak(port);
    }
    ret(ctx, 0);
}

// s32 osPfsChecker(OSPfs* pfs) -- consistency check/repair; the image is
// validated when it's loaded instead.
extern "C" void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx) {
    int port = pfs_port(rdram, arg(ctx, 0));
    ret(ctx, port < 0 ? -port : 0);
}

// s32 osPfsNumFiles(OSPfs* pfs, s32* max_files, s32* files_used)
extern "C" void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* ctx) {
    gpr max_files = arg(ctx, 1);
    gpr files_used = arg(ctx, 2);
    MEM_W(0, max_files) = 0;
    MEM_W(0, files_used) = 0;
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }
    std::lock_guard lock{ pak_mutex };
    int used = 0;
    for (int i = 0; i < kDirEntries; i++) {
        used += !paks[port].dir(i).empty();
    }
    MEM_W(0, max_files) = kDirEntries;
    MEM_W(0, files_used) = used;
    ret(ctx, 0);
}

// s32 osPfsRepairId(OSPfs* pfs)
extern "C" void osPfsRepairId_recomp(uint8_t* rdram, recomp_context* ctx) {
    int port = pfs_port(rdram, arg(ctx, 0));
    ret(ctx, port < 0 ? -port : 0);
}

// s32 osPfsGetLabel(OSPfs* pfs, u8* label, int* len)
// The 32-byte pak label is the start of page 0.
extern "C" void osPfsGetLabel_recomp(uint8_t* rdram, recomp_context* ctx) {
    gpr label = arg(ctx, 1);
    gpr len = arg(ctx, 2);
    int port = pfs_port(rdram, arg(ctx, 0));
    if (port < 0) {
        ret(ctx, -port);
        return;
    }
    std::lock_guard lock{ pak_mutex };
    int n = 0;
    for (int i = 0; i < 32; i++) {
        MEM_B(i, label) = (int8_t)paks[port].data[i];
        if (paks[port].data[i] != 0) {
            n = i + 1;
        }
    }
    MEM_W(0, len) = n;
    ret(ctx, 0);
}

// s32 __osPfsGetStatus(OSMesgQueue* queue, int channel)
// Only reached from the Game Boy Pak functions; there is never a GB Pak.
extern "C" void __osPfsGetStatus_recomp(uint8_t*, recomp_context* ctx) {
    ret(ctx, PFS_ERR_NOPACK);
}
