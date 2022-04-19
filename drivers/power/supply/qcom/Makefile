# SPDX-License-Identifier: GPL-2.0-only

#obj-$(CONFIG_QPNP_QG)			+= qpnp-qg.o pmic-voter.o qg-util.o qg-soc.o qg-sdam.o qg-battery-profile.o qg-profile-lib.o fg-alg.o
# downstream compiled with CONFIG_QPNP_SMB5 and other charging drivers
#obj-y					+= step-chg-jeita.o


obj-$(CONFIG_QPNP_QG) += qpnp-qg2.o
qpnp-qg2-y	:= qpnp-qg.o qg-util.o qg-soc.o qg-sdam.o qg-battery-profile.o qg-profile-lib.o fg-alg.o
obj-y					+= step-chg-jeita.o pmic-voter.o
