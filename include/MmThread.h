/*
 * MmThread.h - Multimedia Class Scheduler registration for real-time audio threads
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

#ifndef LMMS_MM_THREAD_H
#define LMMS_MM_THREAD_H

namespace lmms
{

// Joins the calling thread to MMCSS as "Pro Audio" on Windows.
// Construction is a no-op on every other platform, and it is safe when the
// thread is already registered (PortAudio may have done it first).
class MmThreadRegistration
{
public:
	// raisePriority is for the device callback only. Worker threads keep the
	// task's default priority so a 4-core machine is not packed with HIGH threads.
	explicit MmThreadRegistration(bool raisePriority = false);
	~MmThreadRegistration();

	MmThreadRegistration(const MmThreadRegistration&) = delete;
	MmThreadRegistration(MmThreadRegistration&&) = delete;
	MmThreadRegistration& operator=(const MmThreadRegistration&) = delete;
	MmThreadRegistration& operator=(MmThreadRegistration&&) = delete;

private:
#if defined(_WIN32)
	void* m_handle = nullptr;
#endif
};

} // namespace lmms

#endif // LMMS_MM_THREAD_H
