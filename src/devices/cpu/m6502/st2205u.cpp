// license:BSD-3-Clause
// copyright-holders:AJR
/**********************************************************************

    Sitronix ST2205U 8-Bit Integrated Microcontroller

    Functional blocks:
    * Interrupt controller (15 levels excluding BRK and RESET)
    * GPIO (7 ports, 8 bits each)
    * External bus (up to 7 CS outputs, 48M maximum addressable)
    * Timers/event counters with clocking outputs (4 plus base timer)
    * Programmable sound generator (4 channels with FIFOs, plus PWM
      or ADPCM DAC and 16x8 signed multiplicator)
    * LCD controller (640x400 B/W, 400x320 4-gray, 160xRGBx120 16-gray)
    * Serial peripheral interface
    * UART (built-in BRG; RS-232 and IrDA modes)
    * USB 1.1 (separate control and bulk transfer endpoint buffers)
    * Direct memory access
    * NAND/AND Flash memory interface (includes ECC generator)
    * Power down modes (WAI-0, WAI-1, STP)
    * Watchdog timer
    * Real time clock (seconds, minutes, hours with alarm interrupts)
    * Low voltage detector with reset
    * 512K ROM (may be disabled)
    * 32K SRAM

**********************************************************************/

#include "emu.h"
#include "st2205u.h"

DEFINE_DEVICE_TYPE(ST2205U, st2205u_device, "st2205", "Sitronix ST2205U Integrated Microcontroller")

st2205u_device::st2205u_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: st2xxx_device(mconfig, ST2205U, tag, owner, clock,
					address_map_constructor(FUNC(st2205u_device::int_map), this),
					26, // logical; only 23 address lines are brought out
					0xdfff)
	, m_lvctr(0)
{
}

void st2205u_device::device_start()
{
	std::unique_ptr<mi_st2205u> intf = std::make_unique<mi_st2205u>();
	intf->data = &space(AS_DATA);
	intf->dcache = space(AS_DATA).cache<0, 0, ENDIANNESS_LITTLE>();
	intf->irr_enable = false;
	intf->irr = 0;
	intf->prr = 0;
	intf->drr = 0;
	intf->brr = 0;
	intf->irq_service = false;

	intf->ram = make_unique_clear<u8[]>(0x8000);

	save_item(NAME(m_pdata));
	save_item(NAME(m_pctrl));
	save_item(NAME(m_psel));
	save_item(NAME(m_pfun));
	save_item(NAME(m_sys));
	save_item(NAME(m_pmcr));
	save_item(NAME(m_ireq));
	save_item(NAME(m_iena));
	save_item(NAME(m_lvctr));
	save_item(NAME(intf->irr_enable));
	save_item(NAME(intf->irr));
	save_item(NAME(intf->prr));
	save_item(NAME(intf->drr));
	save_item(NAME(intf->brr));
	save_item(NAME(intf->irq_service));
	save_pointer(NAME(intf->ram), 0x8000);

	mintf = std::move(intf);
	init();

	state_add(ST_IRR, "IRR", downcast<mi_st2205u &>(*mintf).irr).mask(0x87ff);
	state_add(ST_PRR, "PRR", downcast<mi_st2205u &>(*mintf).prr).mask(0x87ff);
	state_add(ST_DRR, "DRR", downcast<mi_st2205u &>(*mintf).drr).mask(0x8fff);
	state_add(ST_BRR, "BRR", downcast<mi_st2205u &>(*mintf).brr).mask(0x9fff);
	state_add<u8>(ST_IREQ, "IREQ", [this]() { return m_ireq; }, [this](u16 data) { m_ireq = data; update_irq_state(); }).mask(m_ireq_mask);
	state_add<u8>(ST_IENA, "IENA", [this]() { return m_iena; }, [this](u16 data) { m_iena = data; update_irq_state(); }).mask(m_ireq_mask);
	for (int i = 0; i < 6; i++)
	{
		state_add(ST_PDA + i, string_format("PD%c", 'A' + i).c_str(), m_pdata[i]);
		state_add(ST_PCA + i, string_format("PC%c", 'A' + i).c_str(), m_pctrl[i]);
		if (i == 2 || i == 4)
			state_add(ST_PSA + i, string_format("PS%c", 'A' + i).c_str(), m_psel[i]);
		if (i == 2 || i == 3)
			state_add(ST_PFC + i - 2, string_format("PF%c", 'A' + i).c_str(), m_pfun[i - 2]).mask(i == 2 ? 0xfe : 0xff);
	}
	state_add(ST_PDL, "PDL", m_pdata[6]);
	state_add(ST_PCL, "PCL", m_pctrl[6]);
	state_add(ST_PMCR, "PMCR", m_pmcr);
	state_add<u8>(ST_SYS, "SYS", [this]() { return m_sys; }, [this](u8 data) { sys_w(data); }).mask(0xfe);
	state_add(ST_LVCTR, "LVCTR", m_lvctr).mask(0x0f);
}

void st2205u_device::device_reset()
{
	st2xxx_device::device_reset();

	mi_st2205u &m = downcast<mi_st2205u &>(*mintf);
	m.irr_enable = false;
	m.irr = 0;
	m.prr = 0;
	m.drr = 0;

	m_lvctr = 0;
}

u8 st2205u_device::mi_st2205u::pread(u16 adr)
{
	u16 bank = irq_service ? irr : prr;
	if (BIT(bank, 15))
		return ram[0x4000 | (adr & 0x3fff)];
	else
		return data->read_byte(u32(bank) << 14 | (adr & 0x3fff));
}

u8 st2205u_device::mi_st2205u::preadc(u16 adr)
{
	u16 bank = irq_service ? irr : prr;
	if (BIT(bank, 15))
		return ram[0x4000 | (adr & 0x3fff)];
	else
		return dcache->read_byte(u32(bank) << 14 | (adr & 0x3fff));
}

void st2205u_device::mi_st2205u::pwrite(u16 adr, u8 val)
{
	u16 bank = irq_service ? irr : prr;
	if (BIT(bank, 15))
		ram[0x4000 | (adr & 0x3fff)] = val;
	else
		data->write_byte(u32(bank) << 14 | (adr & 0x3fff), val);
}

u8 st2205u_device::mi_st2205u::dread(u16 adr)
{
	if (BIT(drr, 15))
		return ram[adr & 0x7fff];
	else
		return data->read_byte(u32(drr) << 15 | (adr & 0x7fff));
}

u8 st2205u_device::mi_st2205u::dreadc(u16 adr)
{
	if (BIT(drr, 15))
		return ram[adr & 0x7fff];
	else
		return dcache->read_byte(u32(drr) << 15 | (adr & 0x7fff));
}

void st2205u_device::mi_st2205u::dwrite(u16 adr, u8 val)
{
	if (BIT(drr, 15))
		ram[adr & 0x7fff] = val;
	else
		data->write_byte(u32(drr) << 15 | (adr & 0x7fff), val);
}

u8 st2205u_device::mi_st2205u::bread(u16 adr)
{
	if (BIT(brr, 15))
		return ram[0x2000 | (adr & 0x1fff)];
	else
		return data->read_byte(u32(brr) << 13 | (adr & 0x1fff));
}

u8 st2205u_device::mi_st2205u::breadc(u16 adr)
{
	if (BIT(brr, 15))
		return ram[0x2000 | (adr & 0x1fff)];
	else
		return dcache->read_byte(u32(brr) << 13 | (adr & 0x1fff));
}

void st2205u_device::mi_st2205u::bwrite(u16 adr, u8 val)
{
	if (BIT(brr, 15))
		ram[0x2000 | (adr & 0x1fff)] = val;
	else
		data->write_byte(u32(brr) << 13 | (adr & 0x1fff), val);
}

u8 st2205u_device::mi_st2205u::read(u16 adr)
{
	return program->read_byte(adr);
}

u8 st2205u_device::mi_st2205u::read_sync(u16 adr)
{
	return BIT(adr, 15) ? dreadc(adr) : BIT(adr, 14) ? preadc(adr) : BIT(adr, 13) ? breadc(adr) : cache->read_byte(adr);
}

u8 st2205u_device::mi_st2205u::read_arg(u16 adr)
{
	return BIT(adr, 15) ? dreadc(adr) : BIT(adr, 14) ? preadc(adr) : BIT(adr, 13) ? breadc(adr) : cache->read_byte(adr);
}

u8 st2205u_device::mi_st2205u::read_vector(u16 adr)
{
	return pread(adr);
}

void st2205u_device::mi_st2205u::write(u16 adr, u8 val)
{
	program->write_byte(adr, val);
}

u8 st2205u_device::irrl_r()
{
	return downcast<mi_st2205u &>(*mintf).irr & 0xff;
}

void st2205u_device::irrl_w(u8 data)
{
	u16 &irr = downcast<mi_st2205u &>(*mintf).irr;
	irr = data | (irr & 0x8f00);
}

u8 st2205u_device::irrh_r()
{
	return downcast<mi_st2205u &>(*mintf).irr >> 8;
}

void st2205u_device::irrh_w(u8 data)
{
	u16 &irr = downcast<mi_st2205u &>(*mintf).irr;
	irr = (data & 0x8f) << 16 | (irr & 0x00ff);
}

u8 st2205u_device::prrl_r()
{
	return downcast<mi_st2205u &>(*mintf).prr & 0xff;
}

void st2205u_device::prrl_w(u8 data)
{
	u16 &prr = downcast<mi_st2205u &>(*mintf).prr;
	prr = data | (prr & 0x8f00);
}

u8 st2205u_device::prrh_r()
{
	return downcast<mi_st2205u &>(*mintf).prr >> 8;
}

void st2205u_device::prrh_w(u8 data)
{
	u16 &prr = downcast<mi_st2205u &>(*mintf).prr;
	prr = (data & 0x8f) << 16 | (prr & 0x00ff);
}

u8 st2205u_device::drrl_r()
{
	return downcast<mi_st2205u &>(*mintf).drr & 0xff;
}

void st2205u_device::drrl_w(u8 data)
{
	u16 &drr = downcast<mi_st2205u &>(*mintf).drr;
	drr = data | (drr & 0x8700);
}

u8 st2205u_device::drrh_r()
{
	return downcast<mi_st2205u &>(*mintf).drr >> 8;
}

void st2205u_device::drrh_w(u8 data)
{
	u16 &drr = downcast<mi_st2205u &>(*mintf).drr;
	drr = (data & 0x87) << 16 | (drr & 0x00ff);
}

u8 st2205u_device::brrl_r()
{
	return downcast<mi_st2205u &>(*mintf).brr & 0xff;
}

void st2205u_device::brrl_w(u8 data)
{
	u16 &brr = downcast<mi_st2205u &>(*mintf).brr;
	brr = data | (brr & 0x9f00);
}

u8 st2205u_device::brrh_r()
{
	return downcast<mi_st2205u &>(*mintf).brr >> 8;
}

void st2205u_device::brrh_w(u8 data)
{
	u16 &brr = downcast<mi_st2205u &>(*mintf).brr;
	brr = (data & 0x9f) << 16 | (brr & 0x00ff);
}

u8 st2205u_device::pmcr_r()
{
	return m_pmcr;
}

void st2205u_device::pmcr_w(u8 data)
{
	m_pmcr = data;
}

u8 st2205u_device::sys_r()
{
	return m_sys;
}

void st2205u_device::sys_w(u8 data)
{
	m_sys = data & 0xfe;
	downcast<mi_st2205u &>(*mintf).irr_enable = BIT(data, 2);
}

u8 st2205u_device::lvctr_r()
{
	return m_lvctr | 0x01;
}

void st2205u_device::lvctr_w(u8 data)
{
	m_lvctr = data & 0x0f;
}

u8 st2205u_device::ram_r(offs_t offset)
{
	return downcast<mi_st2205u &>(*mintf).ram[0x0080 + offset];
}

void st2205u_device::ram_w(offs_t offset, u8 data)
{
	downcast<mi_st2205u &>(*mintf).ram[0x0080 + offset] = data;
}

u8 st2205u_device::pmem_r(offs_t offset)
{
	return downcast<mi_st2205u &>(*mintf).pread(offset);
}

void st2205u_device::pmem_w(offs_t offset, u8 data)
{
	downcast<mi_st2205u &>(*mintf).pwrite(offset, data);
}

u8 st2205u_device::dmem_r(offs_t offset)
{
	return downcast<mi_st2205u &>(*mintf).dread(offset);
}

void st2205u_device::dmem_w(offs_t offset, u8 data)
{
	downcast<mi_st2205u &>(*mintf).dwrite(offset, data);
}

u8 st2205u_device::bmem_r(offs_t offset)
{
	return downcast<mi_st2205u &>(*mintf).bread(offset);
}

void st2205u_device::bmem_w(offs_t offset, u8 data)
{
	downcast<mi_st2205u &>(*mintf).bwrite(offset, data);
}

void st2205u_device::int_map(address_map &map)
{
	map(0x0000, 0x0005).rw(FUNC(st2205u_device::pdata_r), FUNC(st2205u_device::pdata_w));
	map(0x0006, 0x0006).rw(FUNC(st2205u_device::psc_r), FUNC(st2205u_device::psc_w));
	map(0x0007, 0x0007).rw(FUNC(st2205u_device::pse_r), FUNC(st2205u_device::pse_w));
	map(0x0008, 0x000d).rw(FUNC(st2205u_device::pctrl_r), FUNC(st2205u_device::pctrl_w));
	map(0x000e, 0x000e).rw(FUNC(st2205u_device::pfc_r), FUNC(st2205u_device::pfc_w));
	map(0x000f, 0x000f).rw(FUNC(st2205u_device::pfd_r), FUNC(st2205u_device::pfd_w));
	map(0x0030, 0x0030).rw(FUNC(st2205u_device::irrl_r), FUNC(st2205u_device::irrl_w));
	map(0x0031, 0x0031).rw(FUNC(st2205u_device::irrh_r), FUNC(st2205u_device::irrh_w));
	map(0x0032, 0x0032).rw(FUNC(st2205u_device::prrl_r), FUNC(st2205u_device::prrl_w));
	map(0x0033, 0x0033).rw(FUNC(st2205u_device::prrh_r), FUNC(st2205u_device::prrh_w));
	map(0x0034, 0x0034).rw(FUNC(st2205u_device::drrl_r), FUNC(st2205u_device::drrl_w));
	map(0x0035, 0x0035).rw(FUNC(st2205u_device::drrh_r), FUNC(st2205u_device::drrh_w));
	map(0x0036, 0x0036).rw(FUNC(st2205u_device::brrl_r), FUNC(st2205u_device::brrl_w));
	map(0x0037, 0x0037).rw(FUNC(st2205u_device::brrh_r), FUNC(st2205u_device::brrh_w));
	map(0x0039, 0x0039).rw(FUNC(st2205u_device::sys_r), FUNC(st2205u_device::sys_w));
	map(0x003a, 0x003a).rw(FUNC(st2205u_device::pmcr_r), FUNC(st2205u_device::pmcr_w));
	map(0x003c, 0x003c).rw(FUNC(st2205u_device::ireql_r), FUNC(st2205u_device::ireql_w));
	map(0x003d, 0x003d).rw(FUNC(st2205u_device::ireqh_r), FUNC(st2205u_device::ireqh_w));
	map(0x003e, 0x003e).rw(FUNC(st2205u_device::ienal_r), FUNC(st2205u_device::ienal_w));
	map(0x003f, 0x003f).rw(FUNC(st2205u_device::ienah_r), FUNC(st2205u_device::ienah_w));
	map(0x004e, 0x004e).rw(FUNC(st2205u_device::pl_r), FUNC(st2205u_device::pl_w));
	map(0x004f, 0x004f).rw(FUNC(st2205u_device::pcl_r), FUNC(st2205u_device::pcl_w));
	map(0x0057, 0x0057).rw(FUNC(st2205u_device::lvctr_r), FUNC(st2205u_device::lvctr_w));
	map(0x0080, 0x1fff).rw(FUNC(st2205u_device::ram_r), FUNC(st2205u_device::ram_w)); // assumed to be shared with banked RAM
	map(0x2000, 0x3fff).rw(FUNC(st2205u_device::bmem_r), FUNC(st2205u_device::bmem_w));
	map(0x4000, 0x7fff).rw(FUNC(st2205u_device::pmem_r), FUNC(st2205u_device::pmem_w));
	map(0x8000, 0xffff).rw(FUNC(st2205u_device::dmem_r), FUNC(st2205u_device::dmem_w));
}
