/*
 * MmThread.cpp - Multimedia Class Scheduler registration for real-time audio threads
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "MmThread.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <avrt.h>

#include <atomic>
#include <iostream>
#endif

namespace lmms
{

#if defined(_WIN32)

MmThreadRegistration::MmThreadRegistration()
{
	DWORD taskIndex = 0;
	m_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
	if (!m_handle) { return; }

	// HIGH is the usual Pro Audio priority. CRITICAL starves a 4-core 15 W CPU.
	AvSetMmThreadPriority(static_cast<HANDLE>(m_handle), AVRT_PRIORITY_HIGH);

	static std::atomic_flag logged = ATOMIC_FLAG_INIT;
	if (!logged.test_and_set(std::memory_order_relaxed))
	{
		std::cerr << "MMCSS: registered audio thread as \"Pro Audio\"\n";
	}
}

MmThreadRegistration::~MmThreadRegistration()
{
	if (m_handle) { AvRevertMmThreadCharacteristics(static_cast<HANDLE>(m_handle)); }
}

#else

MmThreadRegistration::MmThreadRegistration() = default;
MmThreadRegistration::~MmThreadRegistration() = default;

#endif

} // namespace lmms
