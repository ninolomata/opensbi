/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_system.h>

#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/sys/clint.h>

#define EMUL_PLIC_ADDR			0xc000000
#define EMUL_PLIC_NUM_SOURCES	2
#define EMUL_HART_COUNT			1
#define EMUL_CLINT_ADDR			0x2000000

static struct plic_data plic = {
	.addr = EMUL_PLIC_ADDR,
	.num_src = EMUL_PLIC_NUM_SOURCES,
};

static struct clint_data clint = {
	.addr = EMUL_CLINT_ADDR,
	.first_hartid = 0,
	.hart_count = EMUL_HART_COUNT,
	.has_64bit_mmio = TRUE,
};

/* clang-format on */

static void putc(char c){
	extern int putchar(int ch);
    putchar(c);
}

static int getc(){
    return 0;
}

static struct sbi_console_device emul_console = {
	.name = "emul_uart",
	.console_putc = putc,
	.console_getc = getc
};

void tohost_exit(uintptr_t code);
static void emul_system_down(u32 reset_type, u32 reset_reason)
{
	/* For now nothing to do. */
	tohost_exit(0);
}

static int emul_system_reset_check(u32 type, u32 reason)
{
	return 1;
}

static struct sbi_system_reset_device emul_reset = {
	.name = "emul_reset",
	.system_reset_check = emul_system_reset_check,
	.system_reset = emul_system_down
};
static int emul_early_init(bool cold_boot)
{
	if (cold_boot)
		sbi_system_reset_set_device(&emul_reset);
	return 0;
}

static int emul_final_init(bool cold_boot)
{
	return 0;
}

static int emul_console_init(void)
{
	sbi_console_set_device(&emul_console);
    return 0;
}

static int emul_irqchip_init(bool cold_boot)
{
	int rc;
	u32 hartid = current_hartid();

	if (cold_boot) {
		rc = plic_cold_irqchip_init(&plic);
		if (rc)
			return rc;
	}

	return plic_warm_irqchip_init(&plic, 2 * hartid, 2 * hartid + 1);
}

static int emul_ipi_init(bool cold_boot)
{
	int rc;

	if (cold_boot) {
		rc = clint_cold_ipi_init(&clint);
		if (rc)
			return rc;
	}

	return clint_warm_ipi_init();
}

static int emul_timer_init(bool cold_boot)
{
	int rc;

	if (cold_boot) {
		rc = clint_cold_timer_init(&clint, NULL);
		if (rc)
			return rc;
	}

	return clint_warm_timer_init();
}

const struct sbi_platform_operations platform_ops = {
	.early_init		= emul_early_init,
	.final_init		= emul_final_init,
	.console_init		= emul_console_init,
	.irqchip_init		= emul_irqchip_init,
	.ipi_init		= emul_ipi_init,
	.timer_init		= emul_timer_init
};

const struct sbi_platform platform = {
	.opensbi_version	= OPENSBI_VERSION,
	.platform_version	= SBI_PLATFORM_VERSION(0x0, 0x01),
	.name			= "ESRG CVA6 EMULATOR",
	.features		= SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count		= EMUL_HART_COUNT,
	.hart_stack_size	= SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.platform_ops_addr	= (unsigned long)&platform_ops
};
