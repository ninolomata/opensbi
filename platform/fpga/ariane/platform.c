/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019 FORTH-ICS/CARV
 *				Panagiotis Peristerakis <perister@ics.forth.gr>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/serial/uart8250.h>
#include <sbi_utils/timer/aclint_mtimer.h>
#include <libfdt.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_console.h>
#define ARIANE_UART_ADDR			0x10000000
#define ARIANE_UART_FREQ			50000000
#define ARIANE_UART_BAUDRATE			115200
#define ARIANE_UART_REG_SHIFT			2
#define ARIANE_UART_REG_WIDTH			4
#define ARIANE_PLIC_ADDR			0xc000000
#define ARIANE_PLIC_NUM_SOURCES			3
#define ARIANE_HART_COUNT			1
#define ARIANE_CLINT_ADDR			0x2000000
#define ARIANE_ACLINT_MTIMER_FREQ		1000000
#define ARIANE_ACLINT_MSWI_ADDR			(ARIANE_CLINT_ADDR + \
						 CLINT_MSWI_OFFSET)
#define ARIANE_ACLINT_MTIMER_ADDR		(ARIANE_CLINT_ADDR + \
						 CLINT_MTIMER_OFFSET)
static uint32_t hw_event_count;
static struct plic_data plic = {
	.addr = ARIANE_PLIC_ADDR,
	.num_src = ARIANE_PLIC_NUM_SOURCES,
};

static struct aclint_mswi_data mswi = {
	.addr = ARIANE_ACLINT_MSWI_ADDR,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = ARIANE_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = ARIANE_ACLINT_MTIMER_FREQ,
	.mtime_addr = ARIANE_ACLINT_MTIMER_ADDR +
		      ACLINT_DEFAULT_MTIME_OFFSET,
	.mtime_size = ACLINT_DEFAULT_MTIME_SIZE,
	.mtimecmp_addr = ARIANE_ACLINT_MTIMER_ADDR +
			 ACLINT_DEFAULT_MTIMECMP_OFFSET,
	.mtimecmp_size = ACLINT_DEFAULT_MTIMECMP_SIZE,
	.first_hartid = 0,
	.hart_count = ARIANE_HART_COUNT,
	.has_64bit_mmio = TRUE,
};

/*
 * Ariane platform early initialization.
 */
static int ariane_early_init(bool cold_boot)
{
	/* For now nothing to do. */
	return 0;
}

/*
 * Ariane platform final initialization.
 */
static int ariane_final_init(bool cold_boot)
{
	void *fdt;
    int debugger_off;

    if (!cold_boot)
        return 0;

    fdt = fdt_get_address();
    fdt_fixups(fdt);

    //Delete debugger node
    debugger_off = fdt_node_offset_by_compatible(fdt, 0, "riscv,debug-013");
    if(debugger_off > 0)
        fdt_del_node(fdt,debugger_off);

    return 0;
}

/*
 * Initialize the ariane console.
 */
static int ariane_console_init(void)
{
	return uart8250_init(ARIANE_UART_ADDR,
			     ARIANE_UART_FREQ,
			     ARIANE_UART_BAUDRATE,
			     ARIANE_UART_REG_SHIFT,
			     ARIANE_UART_REG_WIDTH);
}

static int plic_ariane_warm_irqchip_init(int m_cntx_id, int s_cntx_id)
{
	size_t i, ie_words = ARIANE_PLIC_NUM_SOURCES / 32 + 1;

	/* By default, enable all IRQs for M-mode of target HART */
	if (m_cntx_id > -1) {
		for (i = 0; i < ie_words; i++)
			plic_set_ie(&plic, m_cntx_id, i, 1);
	}
	/* Enable all IRQs for S-mode of target HART */
	if (s_cntx_id > -1) {
		for (i = 0; i < ie_words; i++)
			plic_set_ie(&plic, s_cntx_id, i, 1);
	}
	/* By default, enable M-mode threshold */
	if (m_cntx_id > -1)
		plic_set_thresh(&plic, m_cntx_id, 1);
	/* By default, disable S-mode threshold */
	if (s_cntx_id > -1)
		plic_set_thresh(&plic, s_cntx_id, 0);

	return 0;
}

/*
 * Initialize the ariane interrupt controller for current HART.
 */
static int ariane_irqchip_init(bool cold_boot)
{
	u32 hartid = current_hartid();
	int ret;

	if (cold_boot) {
		ret = plic_cold_irqchip_init(&plic);
		if (ret)
			return ret;
	}
	return plic_ariane_warm_irqchip_init(2 * hartid, 2 * hartid + 1);
}

/*
 * Initialize IPI for current HART.
 */
static int ariane_ipi_init(bool cold_boot)
{
	int ret;

	if (cold_boot) {
		ret = aclint_mswi_cold_init(&mswi);
		if (ret)
			return ret;
	}

	return aclint_mswi_warm_init();
}

/*
 * Initialize ariane timer for current HART.
 */
static int ariane_timer_init(bool cold_boot)
{
	int ret;
	sbi_printf("hey 0 \r\n");
	if (cold_boot) {
		ret = aclint_mtimer_cold_init(&mtimer, NULL);
		if (ret)
			return ret;
	}

	return aclint_mtimer_warm_init();
}

static int generic_pmu_init(void)
{
	/* sbi_printf("init pmu \r\n");
	return fdt_pmu_setup(fdt_get_address()); */
	u32 ctr_map[15] = {0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000,0x20000};
	int result;
	uint64_t select_mask = 0xffffffffffffffff;
	uint64_t raw_selector[15] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};
	for(int i = 0; i < 15; i++){
		result = sbi_pmu_add_raw_event_counter_map(raw_selector[i], select_mask, ctr_map[i]);
		if (!result)  {
				hw_event_count++;
		} else {
			sbi_printf("error");
			return -1;
		}
	}
	return result;
}

static uint64_t generic_pmu_xlate_to_mhpmevent(uint32_t event_idx,
					       uint64_t data)
{
	uint64_t evt_val = 0;

	return evt_val;
}

/*
 * Platform descriptor.
 */
const struct sbi_platform_operations platform_ops = {
	.early_init = ariane_early_init,
	.final_init = ariane_final_init,
	.console_init = ariane_console_init,
	.irqchip_init = ariane_irqchip_init,
	.ipi_init = ariane_ipi_init,
	.pmu_init		= generic_pmu_init,
	.pmu_xlate_to_mhpmevent = generic_pmu_xlate_to_mhpmevent,
	.timer_init = ariane_timer_init,
};

const struct sbi_platform platform = {
	.opensbi_version = OPENSBI_VERSION,
	.platform_version = SBI_PLATFORM_VERSION(0x0, 0x01),
	.name = "ARIANE RISC-V",
	.features = SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count = ARIANE_HART_COUNT,
	.hart_stack_size = SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.platform_ops_addr = (unsigned long)&platform_ops
};
