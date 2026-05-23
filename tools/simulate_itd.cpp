// Simulate EHCI iTD offset calculation exactly as ehci.cpp does it
// to find page boundary crossing issues
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>

#define B_PAGE_SIZE 4096

int main() {
	// Our transfer parameters
	size_t dataLength = 25600;  // 32 packets * 800 bytes
	size_t packetSize = 800;
	int packetCount = 32;

	// Simulate buffer at page-aligned address
	uint32_t bufferPhy = 0x10000000;  // page-aligned
	uint32_t currentPhy = bufferPhy;

	printf("=== EHCI iTD Offset Simulation ===\n");
	printf("Buffer: 0x%08x, size=%zu, packetSize=%zu, packets=%d\n\n",
		bufferPhy, dataLength, packetSize, packetCount);

	int itdIndex = 0;
	size_t remaining = dataLength;
	int totalPacket = 0;

	while (remaining > 0) {
		printf("--- iTD #%d (currentPhy=0x%08x) ---\n", itdIndex, currentPhy);

		int pg = 0;
		uint32_t pageBase = currentPhy & 0xfffff000;
		uint32_t offset = currentPhy & 0xfff;

		printf("  buffer_phy[0] = 0x%08x, initial offset = 0x%03x\n",
			pageBase, offset);

		for (int i = 0; i < 8 && remaining > 0; i++) {
			size_t length = std::min(remaining, packetSize);

			uint32_t startAddr = pageBase + offset;  // where HW will start writing
			uint32_t endAddr = startAddr + length - 1;
			bool crossesPage = (endAddr & 0xfffff000) != (startAddr & 0xfffff000);

			printf("  token[%d]: PG=%d OFFSET=0x%03x LEN=%4zu -> addr 0x%08x-0x%08x",
				i, pg, offset, length, startAddr, endAddr);

			if (crossesPage) {
				printf(" *** CROSSES PAGE 0x%08x ***",
					(startAddr & 0xfffff000) + B_PAGE_SIZE);
				// Does buffer_phy[pg+1] contain the correct next page?
				// The code sets it AFTER checking offset > 0xfff
			}

			// Check: is this the position ReadIsochronousDescriptorChain expects?
			size_t expectedKernelOffset = totalPacket * packetSize;
			size_t actualKernelOffset = startAddr - bufferPhy;
			if (actualKernelOffset != expectedKernelOffset) {
				printf(" !! MISMATCH: read expects offset %zu, actual %zu (diff=%d)",
					expectedKernelOffset, actualKernelOffset,
					(int)actualKernelOffset - (int)expectedKernelOffset);
			}

			printf("\n");

			remaining -= length;
			offset += length;
			totalPacket++;

			// Page boundary crossing logic (from ehci.cpp line 1267-1272)
			if (remaining > 0 && offset > 0xfff) {
				uint32_t oldOffset = offset;
				offset -= B_PAGE_SIZE;
				currentPhy += B_PAGE_SIZE;
				pg++;
				pageBase = currentPhy & 0xfffff000;
				printf("         -> page crossing: offset 0x%x -> 0x%x, pg=%d, "
					"buffer_phy[%d]=0x%08x\n",
					oldOffset, offset, pg, pg, pageBase);
			}
		}

		// currentPhy adjustment (from ehci.cpp line 1277)
		currentPhy += (offset & 0xfff) - (currentPhy & 0xfff);
		printf("  after iTD: currentPhy=0x%08x, remaining=%zu\n\n",
			currentPhy, remaining);
		itdIndex++;
	}

	printf("=== Total: %d iTDs, %d packets ===\n", itdIndex, totalPacket);
	return 0;
}
