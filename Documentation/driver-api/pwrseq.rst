.. Copyright 2021 Linaro Ltd.

..   This documentation is free software; you can redistribute
..   it and/or modify it under the terms of the GNU General Public
..   License version 2 as published by the Free Software Foundation.

====================
Power Sequencing API
====================

:Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
:Author: Ulf Hansson <ulf.hansson@linaro.org> (original MMC pwrseq)

Introduction
============

This framework is designed to provide a standard kernel interface to
handle power sequencing requirements for different devices.

The intention is to provide a generic way to handle power sequencing of complex
devices sitting on a variety of busses. First implementation comes from MMC
SDIO/eMMC code, not generified to support other kinds of devices.

Glossary
--------

The pwrseq API uses a number of terms which may not be familiar:

Power sequencer

    Electronic device (or part of it) that supplies power to other devices.
    Unlike regulators (which typically handle single voltage), power sequencer
    handles several voltage inputs. Also it does not provide an exact voltage
    output. Instead it makes sure that the consumers (see below) are powered on
    when required.

Consumer

    Electronic device which consumes power provided by a power sequencer.

Consumer driver interface
=========================

This offers a similar API to the kernel clock or regulator framework. Consumer
drivers use `get <#API-pwrseq-get>`__ and
`put <#API-pwrseq-put>`__ operations to acquire and release
power sequencers. Functions are provided to `power on
<#API-pwrseq-full-power-on>`__ and `power off <#API-pwrseq-power-off>`__ the
power sequencer.

A stub version of this API is provided when the power sequencer framework is
not in use in order to minimise the need to use ifdefs.

Power sequencer driver interface
================================

Drivers for power sequencers register the sequencer within the pwrseq
core, providing operations structures to the core.

API reference
=============

Due to limitations of the kernel documentation framework and the
existing layout of the source code the entire regulator API is
documented here.

.. kernel-doc:: include/linux/pwrseq/consumer.h
   :internal:

.. kernel-doc:: include/linux/pwrseq/driver.h
   :internal:

.. kernel-doc:: include/linux/pwrseq/fallback.h
   :internal:

.. kernel-doc:: drivers/power/pwrseq/core.c
   :export:
