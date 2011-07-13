obj-y                                   += common.o
obj-y                                   += devices.o
obj-y					+= delay.o
obj-y                                   += apbio.o
obj-y                                   += io.o
obj-y                                   += irq.o legacy_irq.o
obj-y					+= syncpt.o
obj-y                                   += clock.o
obj-y                                   += dvfs.o
obj-y                                   += timer.o
obj-y                                   += gpio.o
obj-y                                   += pinmux.o
obj-y                                   += powergate.o
obj-y					+= fuse.o
obj-y					+= kfuse.o
obj-y					+= suspend.o
obj-y					+= mc.o
obj-$(CONFIG_FIQ)			+= fiq.o
obj-$(CONFIG_TEGRA_PWM)			+= pwm.o
obj-$(CONFIG_TEGRA_ARB_SEMAPHORE)	+= arb_sema.o
obj-$(CONFIG_USB_SUPPORT)		+= usb_phy.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += clock.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_clocks.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_emc.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= pinmux-t2-tables.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= suspend-t2.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_save.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_dvfs.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_fuse.o


obj-$(CONFIG_CPU_V7)			+= cortex-a9.o
obj-$(CONFIG_LOCAL_TIMERS)		+= localtimer.o
obj-$(CONFIG_SMP)			+= platsmp.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= headsmp-t2.o
obj-$(CONFIG_TEGRA_SYSTEM_DMA)		+= dma.o
obj-$(CONFIG_CPU_FREQ)                  += cpu-tegra.o
obj-$(CONFIG_CPU_IDLE)			+= cpuidle.o
obj-$(CONFIG_TEGRA_PCI)			+= pcie.o
obj-$(CONFIG_TEGRA_IOVMM)               += iovmm.o
obj-$(CONFIG_TEGRA_IOVMM_GART)          += iovmm-gart.o

obj-${CONFIG_MACH_HARMONY}              += board-harmony.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-panel.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-pinmux.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-pcie.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-power.o

obj-${CONFIG_MACH_PAZ00}		+= board-paz00.o
obj-${CONFIG_MACH_PAZ00}		+= board-paz00-pinmux.o

obj-${CONFIG_MACH_SEABOARD}             += board-seaboard.o
obj-${CONFIG_MACH_SEABOARD}             += board-seaboard-memory.o
obj-${CONFIG_MACH_SEABOARD}             += board-seaboard-panel.o
obj-${CONFIG_MACH_SEABOARD}             += board-seaboard-pinmux.o
obj-${CONFIG_MACH_SEABOARD}             += board-seaboard-power.o
obj-${CONFIG_MACH_SEABOARD}             += board-seaboard-sensors.o
obj-${CONFIG_MACH_KAEN}			+= board-kaen-sensors.o
obj-${CONFIG_MACH_AEBL}			+= board-aebl-sensors.o

obj-${CONFIG_MACH_TEGRA_DT}             += board-dt.o

obj-${CONFIG_MACH_TRIMSLICE}            += board-trimslice.o
obj-${CONFIG_MACH_TRIMSLICE}            += board-trimslice-pinmux.o
