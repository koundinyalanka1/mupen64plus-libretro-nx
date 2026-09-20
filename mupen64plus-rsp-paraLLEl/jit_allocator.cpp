#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif
#include <limits>
#include <algorithm>

#include "jit_allocator.hpp"

namespace RSP
{
namespace JIT
{
#ifdef IOS // iOS/tvOS is 64bit but does not allow an infinite amount of VA space
static constexpr bool huge_va = false;
#else
static constexpr bool huge_va = std::numeric_limits<size_t>::max() > 0x100000000ull;
#endif
// On 64-bit systems, we will never allocate more than one block, this is important since we must ensure that
// relative jumps are reachable in 32-bits.
// We won't actually allocate 1 GB on 64-bit, but just reserve VA space for it, which we have basically an infinite amount of.
static constexpr size_t block_size = huge_va ? (1024 * 1024 * 1024) : (2 * 1024 * 1024);
Allocator::~Allocator()
{
#ifdef _WIN32
	for (auto &block : blocks)
		if (block.code)
			VirtualFree(block.code, 0, MEM_RELEASE);
#else
	for (auto &block : blocks)
		if (block.code)
			munmap(block.code, block.size);
#endif
}

static size_t align_page(size_t offset)
{
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	const size_t page_size = info.dwPageSize;
#else
	const long system_page_size = sysconf(_SC_PAGESIZE);
	if (system_page_size <= 0)
		return 0;
	const size_t page_size = static_cast<size_t>(system_page_size);
#endif
	const size_t page_mask = page_size - 1;
	if (!page_size || (page_size & page_mask) ||
	    offset > std::numeric_limits<size_t>::max() - page_mask)
		return 0;
	return (offset + page_mask) & ~page_mask;
}

static bool commit_read_write(void *ptr, size_t size)
{
#ifdef _WIN32
	return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) == ptr;
#else
	return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
#endif
}

static bool commit_execute(void *ptr, size_t size)
{
#ifdef _WIN32
	DWORD old_protect;
	return VirtualProtect(ptr, size, PAGE_EXECUTE, &old_protect) != 0;
#else
	return mprotect(ptr, size, PROT_EXEC) == 0;
#endif
}

bool Allocator::commit_code(void *code, size_t size)
{
	size = align_page(size);
	return code && size && commit_execute(code, size);
}

void *Allocator::allocate_code(size_t size)
{
	size = align_page(size);
	if (!size)
		return nullptr;
	if (blocks.empty())
		blocks.push_back(reserve_block(std::max(size, block_size)));

	auto *block = &blocks.back();
	if (!block->code)
		return nullptr;

	if (size > block->size - block->offset)
		block = nullptr;

	if (!block)
	{
		if (huge_va)
			abort();
		blocks.push_back(reserve_block(std::max(size, block_size)));
		block = &blocks.back();
	}

	if (!block || !block->code)
		return nullptr;

	void *ret = block->code + block->offset;

	if (!commit_read_write(ret, size))
		return nullptr;
	block->offset += size;
	return ret;
}

Allocator::Block Allocator::reserve_block(size_t size)
{
	Block block;
#ifdef _WIN32
	block.code = static_cast<uint8_t *>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE));
	block.size = size;
	return block;
#else
	block.code = static_cast<uint8_t *>(mmap(nullptr, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE,
	                                         -1, 0));
	if (block.code == MAP_FAILED)
		block.code = nullptr;
	block.size = size;
	return block;
#endif
}
}
}
