#include <unistd.h>
#include <sys/mman.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
alignas(16384) uint8_t memory[65536];
long page_size;
bool fail_map, fail_protect;
unsigned map_calls, protect_calls, unmap_calls;
void *last_protected;
size_t last_protected_size;

void require(bool condition)
{
	if (!condition)
		std::abort();
}

long test_sysconf(int name)
{
	require(name == _SC_PAGESIZE);
	return page_size;
}

void *test_mmap(void *, size_t size, int protection, int, int, off_t)
{
	++map_calls;
	require(size >= sizeof(memory));
	require(protection == PROT_NONE);
	return fail_map ? MAP_FAILED : memory;
}

int test_mprotect(void *address, size_t size, int)
{
	++protect_calls;
	last_protected = address;
	last_protected_size = size;
	if (fail_protect || reinterpret_cast<uintptr_t>(address) % page_size || size % page_size)
		return -1;
	return 0;
}

int test_munmap(void *address, size_t)
{
	++unmap_calls;
	require(address == memory);
	return 0;
}

void reset(long size)
{
	page_size = size;
	fail_map = fail_protect = false;
	map_calls = protect_calls = unmap_calls = 0;
	last_protected = nullptr;
	last_protected_size = 0;
}
}

// Intercept only the operating-system boundary; exercise the real allocator.
#define sysconf test_sysconf
#define mmap test_mmap
#define mprotect test_mprotect
#define munmap test_munmap
#include "../mupen64plus-rsp-paraLLEl/jit_allocator.cpp"
#undef sysconf
#undef mmap
#undef mprotect
#undef munmap

int main()
{
	using RSP::JIT::Allocator;
	for (long size : {4096L, 16384L})
	{
		reset(size);
		{
			Allocator allocator;
			void *first = allocator.allocate_code(1);
			require(first == memory);
			require(last_protected_size == static_cast<size_t>(size));
			void *second = allocator.allocate_code(size + 1);
			require(second == memory + size);
			require(last_protected_size == static_cast<size_t>(2 * size));
			require(Allocator::commit_code(first, 1));
			require(last_protected == first && last_protected_size == static_cast<size_t>(size));
			require(Allocator::commit_code(second, size + 1));
			require(last_protected == second && last_protected_size == static_cast<size_t>(2 * size));
			require(map_calls == 1);
		}
		require(unmap_calls == 1);

		reset(size);
		{
			Allocator allocator;
			fail_map = true;
			require(allocator.allocate_code(1) == nullptr);
			require(protect_calls == 0);
		}
		require(unmap_calls == 0);

		reset(size);
		{
			Allocator allocator;
			fail_protect = true;
			require(allocator.allocate_code(1) == nullptr);
			fail_protect = false;
			// A failed protection change must not consume address space.
			require(allocator.allocate_code(1) == memory);
			fail_protect = true;
			require(!Allocator::commit_code(memory, 1));
		}
		require(unmap_calls == 1);

		reset(size);
		{
			Allocator allocator;
			for (size_t invalid : {size_t(0), std::numeric_limits<size_t>::max()})
			{
				require(allocator.allocate_code(invalid) == nullptr);
				require(!Allocator::commit_code(memory, invalid));
			}
			require(!Allocator::commit_code(nullptr, 1));
			require(map_calls == 0 && protect_calls == 0);
		}
	}
	for (long invalid : {-1L, 0L, 3L})
	{
		reset(invalid);
		Allocator allocator;
		require(allocator.allocate_code(1) == nullptr);
		require(!Allocator::commit_code(memory, 1));
		require(map_calls == 0 && protect_calls == 0);
	}
	std::puts("JIT allocator page alignment and failure tests passed");
}
