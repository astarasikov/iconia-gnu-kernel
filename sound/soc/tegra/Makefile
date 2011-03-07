# Tegra platform Support
snd-soc-tegra-das-objs := tegra_das.o
snd-soc-tegra-pcm-objs := tegra_pcm.o
snd-soc-tegra-i2s-objs := tegra_i2s.o
snd-soc-tegra-spdif-objs := tegra_spdif.o
snd-soc-tegra-utils-objs += tegra_asoc_utils.o

obj-$(CONFIG_SND_TEGRA_SOC) += snd-soc-tegra-utils.o
obj-$(CONFIG_SND_TEGRA_SOC) += snd-soc-tegra-das.o
obj-$(CONFIG_SND_TEGRA_SOC) += snd-soc-tegra-pcm.o
obj-$(CONFIG_SND_TEGRA_SOC_I2S) += snd-soc-tegra-i2s.o
obj-$(CONFIG_SND_TEGRA_SOC_SPDIF) += snd-soc-tegra-spdif.o

# Tegra machine Support
snd-soc-tegra-harmony-objs := harmony.o
snd-soc-tegra-seaboard-objs := seaboard.o

obj-$(CONFIG_SND_TEGRA_SOC_HARMONY) += snd-soc-tegra-harmony.o
obj-$(CONFIG_SND_TEGRA_SOC_SEABOARD) += snd-soc-tegra-seaboard.o
