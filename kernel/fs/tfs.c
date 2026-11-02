#include "Kernel.h"

// Small helpers

static int tfs_strlen(const char *s) __attribute__((unused));
static int tfs_strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}
static int tfs_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a - *b;
}
static void tfs_strcpy(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1)
    {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void tfs_memset(void *p, uint8_t v, size_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = v;
}
static void tfs_memcpy(void *d, const void *s, size_t n)
{
    uint8_t *bd = d;
    const uint8_t *bs = s;
    while (n--)
        *bd++ = *bs++;
}

// Internal state

static tfs_super_t _super;
static int _mounted = 0;

#define TFS_NODE_POOL_SIZE 128
static vfs_node_t *_root_node = 0;
static vfs_node_t _node_pool[TFS_NODE_POOL_SIZE];
static uint8_t _node_used[TFS_NODE_POOL_SIZE];

static vfs_node_t *node_alloc(void)
{
    for (int i = 0; i < TFS_NODE_POOL_SIZE; i++)
    {
        if (!_node_used[i])
        {
            _node_used[i] = 1;
            tfs_memset(&_node_pool[i], 0, sizeof(vfs_node_t));
            return &_node_pool[i];
        }
    }
    return 0; /* pool exhausted */
}

static void tfs_close(vfs_node_t *node)
{
    if (node == _root_node)
        return; // root node is never freed
    for (int i = 0; i < TFS_NODE_POOL_SIZE; i++)
    {
        if (&_node_pool[i] == node)
        {
            _node_used[i] = 0;
            return;
        }
    }
}

// Sector and block I/O

static int read_sector(uint32_t lba, void *buf)
{
    return disk_read(lba, 1, buf) ? 0 : -1;
}
static int write_sector(uint32_t lba, const void *buf)
{
    int r = disk_write(lba, 1, buf) ? 0 : -1;
    if (r < 0)
        kprintf("tfs: write_sector FAILED lba=%d\n", lba);
    return r;
}

// Read or write one full 4 KB data block (8 sectors).
static int read_block(uint32_t blk, void *buf)
{
    uint32_t lba = TFS_LBA_DATA + blk * TFS_BLOCK_SECTORS;
    int r = disk_read(lba, TFS_BLOCK_SECTORS, buf) ? 0 : -1;
    if (r < 0)
        kprintf("tfs: read_block FAILED blk=%d lba=%d\n", blk, lba);
    return r;
}
static int write_block(uint32_t blk, const void *buf)
{
    uint32_t lba = TFS_LBA_DATA + blk * TFS_BLOCK_SECTORS;
    const uint8_t *p = (const uint8_t *)buf;
    for (int s = 0; s < TFS_BLOCK_SECTORS; s++)
    {
        if (!disk_write(lba + s, 1, p + s * 512))
        {
            kprintf("tfs: write_block FAILED sector %d\n", s);
            return -1;
        }
    }
    return 0;
}

static int32_t block_alloc(void);

// returns fs data-block index, or 0 if hole / error
static uint32_t tfs_block_at(const tfs_inode_t *ino, uint32_t blk_idx)
{
    if (blk_idx < TFS_NUM_DIRECT)
        return ino->blocks[blk_idx];

    uint32_t ind = ino->blocks[TFS_NUM_DIRECT]; // slot 9
    if (!ind)
        return 0;

    static uint8_t ibuf[TFS_BLOCK_SIZE];
    if (read_block(ind, ibuf) < 0)
        return 0;

    uint32_t *ptrs = (uint32_t *)ibuf;
    uint32_t ii = blk_idx - TFS_NUM_DIRECT;
    if (ii >= TFS_INDIRECT_PTRS)
        return 0;
    return ptrs[ii];
}

static int tfs_get_or_alloc(tfs_inode_t *ino, uint32_t blk_idx, uint32_t *out)
{
    if (blk_idx < TFS_NUM_DIRECT)
    {
        if (!ino->blocks[blk_idx])
        {
            uint32_t b = block_alloc();
            if (!b)
                return -1;
            ino->blocks[blk_idx] = b;
        }
        *out = ino->blocks[blk_idx];
        return 0;
    }

    uint32_t ii = blk_idx - TFS_NUM_DIRECT;
    if (ii >= TFS_INDIRECT_PTRS)
        return -1;

    if (!ino->blocks[TFS_NUM_DIRECT])
    {
        uint32_t ib = block_alloc();
        if (!ib)
            return -1;
        static uint8_t zero[TFS_BLOCK_SIZE];
        tfs_memset(zero, 0, TFS_BLOCK_SIZE);
        if (write_block(ib, zero) < 0)
            return -1;
        ino->blocks[TFS_NUM_DIRECT] = ib;
    }

    static uint8_t ibuf[TFS_BLOCK_SIZE];
    if (read_block(ino->blocks[TFS_NUM_DIRECT], ibuf) < 0)
        return -1;

    uint32_t *ptrs = (uint32_t *)ibuf;
    if (!ptrs[ii])
    {
        uint32_t b = block_alloc();
        if (!b)
            return -1;
        ptrs[ii] = b;
        if (write_block(ino->blocks[TFS_NUM_DIRECT], ibuf) < 0)
            return -1;
    }
    *out = ptrs[ii];
    return 0;
}

// Superblock

static int super_read(void)
{
    uint8_t buf[TFS_SECTOR_SIZE];
    if (read_sector(TFS_LBA_SUPER, buf) < 0)
        return -1;
    tfs_memcpy(&_super, buf, sizeof(_super));
    return 0;
}
static int super_write(void)
{
    uint8_t buf[TFS_SECTOR_SIZE];
    tfs_memset(buf, 0, sizeof(buf));
    tfs_memcpy(buf, &_super, sizeof(_super));
    return write_sector(TFS_LBA_SUPER, buf);
}

// Bitmap

// The bitmap uses 8 sectors, or 4096 bytes, with one bit per data block.
static uint8_t _bitmap[TFS_BITMAP_SECTORS * TFS_SECTOR_SIZE];

static int bitmap_read(void)
{
    for (int i = 0; i < TFS_BITMAP_SECTORS; i++)
    {
        if (read_sector(TFS_LBA_BITMAP + i, _bitmap + i * TFS_SECTOR_SIZE) < 0)
            return -1;
    }
    return 0;
}
static int bitmap_write(void)
{
    for (int i = 0; i < TFS_BITMAP_SECTORS; i++)
    {
        if (write_sector(TFS_LBA_BITMAP + i, _bitmap + i * TFS_SECTOR_SIZE) < 0)
        {
            kprintf("tfs: bitmap_write FAILED sector %d\n", i);
            return -1;
        }
    }
    return 0;
}
static int bitmap_get(uint32_t blk) { return (_bitmap[blk / 8] >> (blk % 8)) & 1; }
static void bitmap_set(uint32_t blk) { _bitmap[blk / 8] |= (1 << (blk % 8)); }
static void bitmap_clr(uint32_t blk) { _bitmap[blk / 8] &= ~(1 << (blk % 8)); }

static int32_t block_alloc(void)
{
    for (uint32_t i = 1; i < _super.total_blocks; i++)
    { /* Skip block 0; it is reserved as the null sentinel. */
        if (!bitmap_get(i))
        {
            bitmap_set(i);
            if (bitmap_write() < 0)
            {
                kprintf("tfs: block_alloc bitmap_write failed\n");
                return -1;
            }
            _super.free_blocks--;
            super_write();
            // Fill the new block with zeros.
            static uint8_t zero[TFS_BLOCK_SIZE];
            tfs_memset(zero, 0, TFS_BLOCK_SIZE);
            if (write_block(i, zero) < 0)
            {
                kprintf("tfs: block_alloc zero failed blk=%d\n", i);
                return -1;
            }
            kprintf("tfs: block_alloc blk=%d\n", i);
            return (int32_t)i;
        }
    }
    return -1; // disk full
}
static void block_free(uint32_t blk)
{
    bitmap_clr(blk);
    bitmap_write();
    _super.free_blocks++;
    super_write();
}

// Inode table

static int inode_read(uint32_t ino, tfs_inode_t *out)
{
    uint32_t sector = TFS_LBA_INODES + (ino * TFS_INODE_SIZE) / TFS_SECTOR_SIZE;
    uint32_t offset = (ino * TFS_INODE_SIZE) % TFS_SECTOR_SIZE;
    uint8_t buf[TFS_SECTOR_SIZE];
    if (read_sector(sector, buf) < 0)
        return -1;
    tfs_memcpy(out, buf + offset, sizeof(tfs_inode_t));
    return 0;
}
static int inode_write(uint32_t ino, const tfs_inode_t *in)
{
    uint32_t sector = TFS_LBA_INODES + (ino * TFS_INODE_SIZE) / TFS_SECTOR_SIZE;
    uint32_t offset = (ino * TFS_INODE_SIZE) % TFS_SECTOR_SIZE;
    uint8_t buf[TFS_SECTOR_SIZE];
    if (read_sector(sector, buf) < 0)
    {
        kprintf("tfs: inode_write read failed\n");
        return -1;
    }
    tfs_memcpy(buf + offset, in, sizeof(tfs_inode_t));
    int r = write_sector(sector, buf);
    if (r < 0)
        kprintf("tfs: inode_write FAILED\n");
    return r;
}

static int32_t inode_alloc(void)
{
    for (uint32_t i = 0; i < TFS_MAX_INODES; i++)
    {
        tfs_inode_t ino;
        if (inode_read(i, &ino) < 0)
            return -1;
        if (ino.type == TFS_TYPE_FREE)
        {
            _super.free_inodes--;
            super_write();
            return (int32_t)i;
        }
    }
    return -1;
}

// Directory helpers

// Look up a named entry in a directory inode and return the child inode number, or -1.
static int32_t dir_lookup(uint32_t dir_ino, const char *name)
{
    tfs_inode_t dir;
    if (inode_read(dir_ino, &dir) < 0 || dir.type != TFS_TYPE_DIR)
        return -1;

    static uint8_t blkbuf[TFS_BLOCK_SIZE];
    for (int b = 0; b < TFS_MAX_DIRECT; b++)
    {
        if (!dir.blocks[b])
            continue;
        if (read_block(dir.blocks[b], blkbuf) < 0)
            return -1;
        tfs_dirent_t *ents = (tfs_dirent_t *)blkbuf;
        for (int e = 0; e < TFS_DIRENTS_PER_BLOCK; e++)
        {
            if (ents[e].inode && tfs_strcmp(ents[e].name, name) == 0)
                return (int32_t)ents[e].inode;
        }
    }
    return -1;
}

// Add an entry to a directory and return 0 on success.
static int dir_add(uint32_t dir_ino, const char *name, uint32_t child_ino)
{
    tfs_inode_t dir;
    if (inode_read(dir_ino, &dir) < 0)
        return -1;

    static uint8_t blkbuf[TFS_BLOCK_SIZE];

    // Look for an empty slot in the existing directory blocks first.
    for (int b = 0; b < TFS_MAX_DIRECT; b++)
    {
        if (!dir.blocks[b])
            continue;
        if (read_block(dir.blocks[b], blkbuf) < 0)
            return -1;
        tfs_dirent_t *ents = (tfs_dirent_t *)blkbuf;
        for (int e = 0; e < TFS_DIRENTS_PER_BLOCK; e++)
        {
            if (!ents[e].inode)
            {
                ents[e].inode = child_ino;
                tfs_strcpy(ents[e].name, name, TFS_DIRENT_SIZE - 4);
                return write_block(dir.blocks[b], blkbuf);
            }
        }
    }

    // Create a new directory block if needed.
    for (int b = 0; b < TFS_MAX_DIRECT; b++)
    {
        if (!dir.blocks[b])
        {
            int32_t newblk = block_alloc();
            if (newblk < 0)
                return -1;
            dir.blocks[b] = (uint32_t)newblk;
            dir.size += TFS_BLOCK_SIZE;
            inode_write(dir_ino, &dir);
            if (read_block(dir.blocks[b], blkbuf) < 0)
                return -1;
            tfs_dirent_t *ents = (tfs_dirent_t *)blkbuf;
            ents[0].inode = child_ino;
            tfs_strcpy(ents[0].name, name, TFS_DIRENT_SIZE - 4);
            return write_block(dir.blocks[b], blkbuf);
        }
    }
    return -1; // The directory has no room left.
}

// Remove a named entry from a directory.
static int dir_remove(uint32_t dir_ino, const char *name)
{
    tfs_inode_t dir;
    if (inode_read(dir_ino, &dir) < 0)
        return -1;

    static uint8_t blkbuf[TFS_BLOCK_SIZE];
    for (int b = 0; b < TFS_MAX_DIRECT; b++)
    {
        if (!dir.blocks[b])
            continue;
        if (read_block(dir.blocks[b], blkbuf) < 0)
            return -1;
        tfs_dirent_t *ents = (tfs_dirent_t *)blkbuf;
        for (int e = 0; e < TFS_DIRENTS_PER_BLOCK; e++)
        {
            if (ents[e].inode && tfs_strcmp(ents[e].name, name) == 0)
            {
                ents[e].inode = 0;
                ents[e].name[0] = 0;
                return write_block(dir.blocks[b], blkbuf);
            }
        }
    }
    return -1;
}

// ─── VFS operations ───────────────────────────────────────────────────────────

static int tfs_read(vfs_node_t *node, uint64_t off, size_t len, uint8_t *buf)
{
    tfs_inode_t ino;
    if (inode_read(node->inode, &ino) < 0)
        return -1;
    if (off >= ino.size)
        return 0;
    if (off + len > ino.size)
        len = ino.size - off;

    static uint8_t blkbuf[TFS_BLOCK_SIZE];
    size_t done = 0;
    while (done < len)
    {
        uint32_t blk_idx = (uint32_t)((off + done) / TFS_BLOCK_SIZE);
        uint32_t blk_off = (uint32_t)((off + done) % TFS_BLOCK_SIZE);
        uint32_t dblk = tfs_block_at(&ino, blk_idx);
        if (!dblk)
            break;
        if (read_block(dblk, blkbuf) < 0)
            break;
        size_t chunk = TFS_BLOCK_SIZE - blk_off;
        if (chunk > len - done)
            chunk = len - done;
        tfs_memcpy(buf + done, blkbuf + blk_off, chunk);
        done += chunk;
    }
    return (int)done;
}

static int tfs_write(vfs_node_t *node, uint64_t off, size_t len, const uint8_t *buf)
{
    tfs_inode_t ino;
    if (inode_read(node->inode, &ino) < 0)
        return -1;

    // Truncate: len==0 and buf==0 means truncate to zero
    if (len == 0 && buf == 0)
    {
        // direct
        for (int b = 0; b < TFS_NUM_DIRECT; b++)
        {
            if (ino.blocks[b])
            {
                block_free(ino.blocks[b]);
                ino.blocks[b] = 0;
            }
        }
        // indirect
        if (ino.blocks[TFS_NUM_DIRECT])
        {
            static uint8_t ibuf[TFS_BLOCK_SIZE];
            if (read_block(ino.blocks[TFS_NUM_DIRECT], ibuf) == 0)
            {
                uint32_t *ptrs = (uint32_t *)ibuf;
                for (uint32_t i = 0; i < TFS_INDIRECT_PTRS; i++)
                {
                    if (ptrs[i])
                        block_free(ptrs[i]);
                }
            }
            block_free(ino.blocks[TFS_NUM_DIRECT]);
            ino.blocks[TFS_NUM_DIRECT] = 0;
        }
        ino.size = 0;
        node->size = 0;
        inode_write(node->inode, &ino);
        return 0;
    }

    static uint8_t blkbuf[TFS_BLOCK_SIZE];
    size_t done = 0;
    while (done < len)
    {
        uint32_t blk_idx = (uint32_t)((off + done) / TFS_BLOCK_SIZE);
        uint32_t blk_off = (uint32_t)((off + done) % TFS_BLOCK_SIZE);
        uint32_t dblk;

        if (tfs_get_or_alloc(&ino, blk_idx, &dblk) < 0)
            break;

        if (read_block(dblk, blkbuf) < 0)
            break;
        size_t chunk = TFS_BLOCK_SIZE - blk_off;
        if (chunk > len - done)
            chunk = len - done;
        tfs_memcpy(blkbuf + blk_off, buf + done, chunk);
        if (write_block(dblk, blkbuf) < 0)
            break;
        done += chunk;
    }

    if (off + done > ino.size)
        ino.size = (uint32_t)(off + done);
    node->size = ino.size;
    inode_write(node->inode, &ino);
    return (int)done;
}

static int tfs_readdir(vfs_node_t *node, uint32_t idx, char *name_out)
{
    tfs_inode_t dir;
    if (inode_read(node->inode, &dir) < 0)
    {
        kprintf("tfs: readdir inode_read failed\n");
        return -1;
    }

    static uint8_t blkbuf[TFS_BLOCK_SIZE];
    uint32_t count = 0;
    for (int b = 0; b < TFS_MAX_DIRECT; b++)
    {
        if (!dir.blocks[b])
            continue;
        if (read_block(dir.blocks[b], blkbuf) < 0)
            return -1;
        tfs_dirent_t *ents = (tfs_dirent_t *)blkbuf;
        for (int e = 0; e < TFS_DIRENTS_PER_BLOCK; e++)
        {
            if (!ents[e].inode)
                continue;
            if (count == idx)
            {
                tfs_strcpy(name_out, ents[e].name, VFS_NAME_MAX);
                return 0;
            }
            count++;
        }
    }
    return -1;
}

static vfs_ops_t tfs_ops; // forward

static vfs_node_t *tfs_make_vnode(uint32_t ino_num)
{
    tfs_inode_t ino;
    if (inode_read(ino_num, &ino) < 0)
        return NULL;
    vfs_node_t *n = node_alloc();
    if (!n)
        return NULL;
    n->inode = ino_num;
    n->size = ino.size;
    n->type = (ino.type == TFS_TYPE_DIR) ? VFS_DIR : VFS_FILE;
    n->ops = &tfs_ops;
    return n;
}

static vfs_node_t *tfs_finddir(vfs_node_t *node, const char *name)
{
    int32_t child = dir_lookup(node->inode, name);
    if (child < 0)
        return NULL;
    vfs_node_t *n = tfs_make_vnode((uint32_t)child);
    if (n)
        tfs_strcpy(n->name, name, VFS_NAME_MAX);
    return n;
}

static int tfs_create(vfs_node_t *node, const char *name, uint32_t type)
{
    // Make sure it doesn't already exist
    if (dir_lookup(node->inode, name) >= 0)
        return -1;

    int32_t new_ino = inode_alloc();
    if (new_ino < 0)
        return -1;

    tfs_inode_t ni;
    tfs_memset(&ni, 0, sizeof(ni));
    ni.type = (type == VFS_DIR) ? TFS_TYPE_DIR : TFS_TYPE_FILE;
    ni.nlinks = 1;
    inode_write((uint32_t)new_ino, &ni);

    int r = dir_add(node->inode, name, (uint32_t)new_ino);
    return r;
}

static int tfs_unlink(vfs_node_t *node, const char *name)
{
    int32_t child_ino = dir_lookup(node->inode, name);
    if (child_ino < 0)
        return -1;

    tfs_inode_t ci;
    if (inode_read((uint32_t)child_ino, &ci) < 0)
        return -1;

    // Free all data blocks
    for (int b = 0; b < TFS_MAX_DIRECT; b++)
    {
        if (ci.blocks[b])
            block_free(ci.blocks[b]);
    }

    // Free inode
    ci.type = TFS_TYPE_FREE;
    tfs_memset(&ci, 0, sizeof(ci));
    inode_write((uint32_t)child_ino, &ci);
    _super.free_inodes++;
    super_write();

    return dir_remove(node->inode, name);
}

static vfs_ops_t tfs_ops = {
    .read = tfs_read,
    .write = tfs_write,
    .readdir = tfs_readdir,
    .finddir = tfs_finddir,
    .create = tfs_create,
    .unlink = tfs_unlink,
    .close = tfs_close,
};

// ─── Root node (static — never freed) ────────────────────────────────────────

vfs_node_t *tfs_get_root(void)
{
    return _root_node;
}

// ─── Mount ────────────────────────────────────────────────────────────────────

int tfs_mount(void)
{
    kprintf("tfs: LBA_DATA=%d BLOCK_SECTORS=%d\n", TFS_LBA_DATA, TFS_BLOCK_SECTORS);
    if (super_read() < 0)
    {
        kprintf("tfs: disk read failed\n");
        return -1;
    }
    if (_super.magic != TFS_MAGIC)
    {
        kprintf("tfs: bad magic (got 0x%x, want 0x%x)\n", _super.magic, TFS_MAGIC);
        return -1;
    }
    if (bitmap_read() < 0)
    {
        kprintf("tfs: bitmap read failed\n");
        return -1;
    }

    kprintf("tfs: mounted — %u blocks, %u free, %u inodes\n",
            _super.total_blocks, _super.free_blocks, _super.total_inodes);

    _mounted = 1;

    // Build root vfs_node
    tfs_inode_t root;
    inode_read(TFS_ROOT_INODE, &root);
    static vfs_node_t _root_static;
    tfs_memset(&_root_static, 0, sizeof(_root_static));
    _root_node = &_root_static;
    _root_node->type = VFS_DIR;
    _root_node->inode = TFS_ROOT_INODE;
    _root_node->size = root.size;
    _root_node->ops = &tfs_ops;
    tfs_strcpy(_root_node->name, "/", VFS_NAME_MAX);

    return 0;
}

// ─── Format (callable from kernel for mkfs command) ──────────────────────────

int tfs_format(uint32_t total_blocks)
{
    // Write superblock
    tfs_memset(&_super, 0, sizeof(_super));
    _super.magic = TFS_MAGIC;
    _super.version = TFS_VERSION;
    _super.total_blocks = total_blocks;
    _super.free_blocks = total_blocks;
    _super.total_inodes = TFS_MAX_INODES;
    _super.free_inodes = TFS_MAX_INODES - 1; // root takes one
    if (super_write() < 0)
        return -1;

    // Clear bitmap, then reserve block 0 as null sentinel
    tfs_memset(_bitmap, 0, sizeof(_bitmap));
    _bitmap[0] |= 0x01; // block 0 reserved — 0 means "no block" in inodes
    _super.free_blocks--;
    if (bitmap_write() < 0)
        return -1;

    // Clear inode table
    uint8_t zero_sector[TFS_SECTOR_SIZE];
    tfs_memset(zero_sector, 0, TFS_SECTOR_SIZE);
    for (int i = 0; i < (int)TFS_INODE_SECTORS; i++)
    {
        write_sector(TFS_LBA_INODES + i, zero_sector);
    }

    // Create root inode (inode 0)
    tfs_inode_t root;
    tfs_memset(&root, 0, sizeof(root));
    root.type = TFS_TYPE_DIR;
    root.nlinks = 1;
    inode_write(TFS_ROOT_INODE, &root);

    kprintf("tfs: formatted — %u data blocks\n", total_blocks);
    return 0;
}