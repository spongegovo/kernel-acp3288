//======================================================================
// Driver for the Epson RTC module RX-8010 SJ
//
// Copyright(C) SEIKO EPSON CORPORATION 2013. All rights reserved.
//
// Derived from RX-8025 driver:
// Copyright (C) 2009 Wolfgang Grandegger <wg@grandegger.com>
//
// Copyright (C) 2005 by Digi International Inc.
// All rights reserved.
//
// Modified by fengjh at rising.com.cn
// <http://lists.lm-sensors.org/mailman/listinfo/lm-sensors>
// 2006.11
//
// Code cleanup by Sergei Poselenov, <sposelenov@emcraft.com>
// Converted to new style by Wolfgang Grandegger <wg@grandegger.com>
// Alarm and periodic interrupt added by Dmitry Rakhchev <rda@emcraft.com>
//
//
// This driver software is distributed as is, without any warranty of any kind,
// either express or implied as further specified in the GNU Public License. This
// software may be used and distributed according to the terms of the GNU Public
// License, version 2 as published by the Free Software Foundation.
// See the file COPYING in the main directory of this archive for more details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <http://www.gnu.org/licenses/>.
//======================================================================
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/rtc.h>

// RX-8010 Register definitions
#define RX8010_REG_SEC		0x10
#define RX8010_REG_MIN		0x11
#define RX8010_REG_HOUR		0x12
#define RX8010_REG_WDAY		0x13
#define RX8010_REG_MDAY		0x14
#define RX8010_REG_MONTH	0x15
#define RX8010_REG_YEAR		0x16
// 0x17 is reserved
#define RX8010_REG_ALMIN	0x18
#define RX8010_REG_ALHOUR	0x19
#define RX8010_REG_ALWDAY	0x1A
#define RX8010_REG_TCOUNT0	0x1B
#define RX8010_REG_TCOUNT1	0x1C
#define RX8010_REG_EXT		0x1D
#define RX8010_REG_FLAG		0x1E
#define RX8010_REG_CTRL		0x1F
#define RX8010_REG_USER0	0x20
#define RX8010_REG_USER1	0x21
#define RX8010_REG_USER2	0x22
#define RX8010_REG_USER3	0x23
#define RX8010_REG_USER4	0x24
#define RX8010_REG_USER5	0x25
#define RX8010_REG_USER6	0x26
#define RX8010_REG_USER7	0x27
#define RX8010_REG_USER8	0x28
#define RX8010_REG_USER9	0x29
#define RX8010_REG_USERA	0x2A
#define RX8010_REG_USERB	0x2B
#define RX8010_REG_USERC	0x2C
#define RX8010_REG_USERD	0x2D
#define RX8010_REG_USERE	0x2E
#define RX8010_REG_USERF	0x2F
// 0x30 is reserved
// 0x31 is reserved
#define RX8010_REG_IRQ		0x32

// Extension Register (1Dh) bit positions
#define RX8010_BIT_EXT_TSEL		(7 << 0)
#define RX8010_BIT_EXT_WADA		(1 << 3)
#define RX8010_BIT_EXT_TE		(1 << 4)
#define RX8010_BIT_EXT_USEL		(1 << 5)
#define RX8010_BIT_EXT_FSEL		(3 << 6)

// Flag Register (1Eh) bit positions
#define RX8010_BIT_FLAG_VLF		(1 << 1)
#define RX8010_BIT_FLAG_AF		(1 << 3)
#define RX8010_BIT_FLAG_TF		(1 << 4)
#define RX8010_BIT_FLAG_UF		(1 << 5)

// Control Register (1Fh) bit positions
#define RX8010_BIT_CTRL_TSTP	(1 << 2)
#define RX8010_BIT_CTRL_AIE		(1 << 3)
#define RX8010_BIT_CTRL_TIE		(1 << 4)
#define RX8010_BIT_CTRL_UIE		(1 << 5)
#define RX8010_BIT_CTRL_STOP	(1 << 6)
#define RX8010_BIT_CTRL_TEST	(1 << 7)


static const struct i2c_device_id rx8010_id[] = {
	{ "rx8010", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rx8010_id);

struct rx8010_data {
	struct i2c_client *client;
	struct rtc_device *rtc;
	struct work_struct work;
	u8 ctrlreg;
	unsigned exiting:1;
};


//----------------------------------------------------------------------
// rx8010_read_reg()
// reads a rx8010 register (see Register defines)
// See also rx8010_read_regs() to read multiple registers.
//
//----------------------------------------------------------------------
static int rx8010_read_reg(struct i2c_client *client, int number, u8 *value)
{
	int ret = i2c_smbus_read_byte_data(client, number) ;

	//check for error
	if (ret < 0) {
		dev_err(&client->dev, "Unable to read register #%d\n", number);
		return ret;
	}

	*value = ret;
	return 0;
}

//----------------------------------------------------------------------
// rx8010_read_regs()
// reads a specified number of rx8010 registers (see Register defines)
// See also rx8010_read_reg() to read single register.
//
//----------------------------------------------------------------------
static int rx8010_read_regs(struct i2c_client *client, int number, u8 length, u8 *values)
{
	int ret = i2c_smbus_read_i2c_block_data(client, number, length, values);

	//check for length error
	if (ret != length) {
		dev_err(&client->dev, "Unable to read registers #%d..#%d\n", number, number + length - 1);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

//----------------------------------------------------------------------
// rx8010_write_reg()
// writes a rx8010 register (see Register defines)
// See also rx8010_write_regs() to write multiple registers.
//
//----------------------------------------------------------------------
static int rx8010_write_reg(struct i2c_client *client, int number, u8 value)
{
	int ret = i2c_smbus_write_byte_data(client, number, value);

	//check for error
	if (ret)
		dev_err(&client->dev, "Unable to write register #%d\n", number);

	return ret;
}

//----------------------------------------------------------------------
// rx8010_write_regs()
// writes a specified number of rx8010 registers (see Register defines)
// See also rx8010_write_reg() to write a single register.
//
//----------------------------------------------------------------------
static int rx8010_write_regs(struct i2c_client *client, int number, u8 length, u8 *values)
{
	int ret = i2c_smbus_write_i2c_block_data(client, number, length, values);

	//check for error
	if (ret)
		dev_err(&client->dev, "Unable to write registers #%d..#%d\n", number, number + length - 1);

	return ret;
}

//----------------------------------------------------------------------
// rx8010_irq()
// irq handler
//
//----------------------------------------------------------------------
static irqreturn_t rx8010_irq(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	struct rx8010_data *rx8010 = i2c_get_clientdata(client);

	disable_irq_nosync(irq);
	schedule_work(&rx8010->work);
	return IRQ_HANDLED;
}

//----------------------------------------------------------------------
// rx8010_work()
//
//----------------------------------------------------------------------
static void rx8010_work(struct work_struct *work)
{
	struct rx8010_data *rx8010 = container_of(work, struct rx8010_data, work);
	struct i2c_client *client = rx8010->client;
	struct mutex *lock = &rx8010->rtc->ops_lock;
	u8 status;

	mutex_lock(lock);

	if (rx8010_read_reg(client, RX8010_REG_FLAG, &status))
		goto out;

	// check VLF
	if ((status & RX8010_BIT_FLAG_VLF))
		dev_warn(&client->dev, "Oscillation stop was detected,"
			 "you may have to readjust the clock\n");

	// periodic "fixed-cycle" timer
	if (status & RX8010_BIT_FLAG_TF) {
		status &= ~RX8010_BIT_FLAG_TF;
		local_irq_disable();
		rtc_update_irq(rx8010->rtc, 1, RTC_PF | RTC_IRQF);
		local_irq_enable();
	}

	// alarm function
	if (status & RX8010_BIT_FLAG_AF) {
		status &= ~RX8010_BIT_FLAG_AF;
		local_irq_disable();
		rtc_update_irq(rx8010->rtc, 1, RTC_AF | RTC_IRQF);
		local_irq_enable();
	}

	// time update function
	if (status & RX8010_BIT_FLAG_UF) {
		status &= ~RX8010_BIT_FLAG_UF;
		local_irq_disable();
		rtc_update_irq(rx8010->rtc, 1, RTC_UF | RTC_IRQF);
		local_irq_enable();
	}

	// acknowledge IRQ
	rx8010_write_reg(client, RX8010_REG_FLAG, status);		//clear flags

out:
	if (!rx8010->exiting)
		enable_irq(client->irq);

	mutex_unlock(lock);
}

//----------------------------------------------------------------------
// rx8010_get_time()
// gets the current time from the rx8010 registers
//
//----------------------------------------------------------------------
static int rx8010_get_time(struct device *dev, struct rtc_time *dt)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 date[7];
	int err;

	err = rx8010_read_regs(rx8010->client, RX8010_REG_SEC, 7, date);
	if (err)
		return err;

	dev_dbg(dev, "%s: read 0x%02x 0x%02x "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
		date[0], date[1], date[2], date[3], date[4], date[5], date[6]);

	//Note: need to subtract 0x10 for index as register offset starts at 0x10
	dt->tm_sec = bcd2bin(date[RX8010_REG_SEC-0x10] & 0x7f);
	dt->tm_min = bcd2bin(date[RX8010_REG_MIN-0x10] & 0x7f);
	dt->tm_hour = bcd2bin(date[RX8010_REG_HOUR-0x10] & 0x3f);	//only 24-hour clock
	dt->tm_mday = bcd2bin(date[RX8010_REG_MDAY-0x10] & 0x3f);
	dt->tm_mon = bcd2bin(date[RX8010_REG_MONTH-0x10] & 0x1f) - 1;
	dt->tm_year = bcd2bin(date[RX8010_REG_YEAR-0x10]);
	dt->tm_wday = bcd2bin(date[RX8010_REG_WDAY-0x10] & 0x7f);

	if (dt->tm_year < 70)
		dt->tm_year += 100;

	dev_dbg(dev, "%s: date %ds %dm %dh %dmd %dm %dy\n", __func__,
		dt->tm_sec, dt->tm_min, dt->tm_hour,
		dt->tm_mday, dt->tm_mon, dt->tm_year);

	return rtc_valid_tm(dt);
}

//----------------------------------------------------------------------
// rx8010_set_time()
// Sets the current time in the rx8010 registers
//
// BUG: The HW assumes every year that is a multiple of 4 to be a leap
// year. Next time this is wrong is 2100, which will not be a leap year
//
// Note: If STOP is not set/cleared, the clock will start when the seconds
//       register is written
//
//----------------------------------------------------------------------
static int rx8010_set_time(struct device *dev, struct rtc_time *dt)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 date[7];
	u8 ctrl;
	int ret;

	//set STOP bit before changing clock/calendar
	rx8010_read_reg(rx8010->client, RX8010_REG_CTRL, &ctrl);
	rx8010->ctrlreg = ctrl | RX8010_BIT_CTRL_STOP;
	rx8010_write_reg(rx8010->client, RX8010_REG_CTRL, rx8010->ctrlreg);

	//Note: need to subtract 0x10 for index as register offset starts at 0x10
	date[RX8010_REG_SEC-0x10] = bin2bcd(dt->tm_sec);
	date[RX8010_REG_MIN-0x10] = bin2bcd(dt->tm_min);
	date[RX8010_REG_HOUR-0x10] = bin2bcd(dt->tm_hour);		//only 24hr time

	date[RX8010_REG_MDAY-0x10] = bin2bcd(dt->tm_mday);
	date[RX8010_REG_MONTH-0x10] = bin2bcd(dt->tm_mon + 1);
	date[RX8010_REG_YEAR-0x10] = bin2bcd(dt->tm_year % 100);
	date[RX8010_REG_WDAY-0x10] = bin2bcd(dt->tm_wday);

	dev_dbg(dev, "%s: write 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		__func__, date[0], date[1], date[2], date[3], date[4], date[5], date[6]);

	ret =  rx8010_write_regs(rx8010->client, RX8010_REG_SEC, 7, date);

	//clear STOP bit after changing clock/calendar
	rx8010_read_reg(rx8010->client, RX8010_REG_CTRL, &ctrl);
	rx8010->ctrlreg = ctrl & ~RX8010_BIT_CTRL_STOP;
	rx8010_write_reg(rx8010->client, RX8010_REG_CTRL, rx8010->ctrlreg);

	return ret;
}

//----------------------------------------------------------------------
// rx8010_init_client()
// initializes the rx8010
//
//----------------------------------------------------------------------
static int rx8010_init_client(struct i2c_client *client, int *need_reset)
{
	struct rx8010_data *rx8010 = i2c_get_clientdata(client);
	u8 ctrl[3];
	int need_clear = 0;
	int err;

	//set reserved register 0x17 with specified value of 0xD8
	err = rx8010_write_reg(client, 0x17, 0xD8);
	if (err)
		goto out;

	//set reserved register 0x30 with specified value of 0x00
	err = rx8010_write_reg(client, 0x30, 0x00);
	if (err)
		goto out;

	//set reserved register 0x31 with specified value of 0x08
	err = rx8010_write_reg(client, 0x30, 0x08);
	if (err)
		goto out;

	//get current extension, flag, control register values
	err = rx8010_read_regs(rx8010->client, RX8010_REG_EXT, 3, ctrl);
	if (err)
		goto out;

	//set extension register, TE to 0, FSEL1-0 and TSEL2-0 for desired frequency
	ctrl[0] &= ~RX8010_BIT_EXT_TE;			//set TE to 0
	ctrl[0] &= ~RX8010_BIT_EXT_FSEL;		//set to 0 (off) for this case
	ctrl[0] |= 0x02;						//set TSEL for 1Hz 
	err = rx8010_write_reg(client, RX8010_REG_EXT, ctrl[0]);
	if (err)
		goto out;

	//set "test bit" and reserved bits of control register zero
	rx8010->ctrlreg = (ctrl[2] & ~RX8010_BIT_CTRL_TEST) & 0xFC;		//bits 1-0 are reseved

	//check for VLF Flag (set at power-on)
	if ((ctrl[1] & RX8010_BIT_FLAG_VLF)) {
		dev_warn(&client->dev, "VLF Flag set,"
			 "you may have to re-adjust the clock\n");
		*need_reset = 1;
	}

	//check for Alarm Flag
	if (ctrl[1] & RX8010_BIT_FLAG_AF) {
		dev_warn(&client->dev, "Alarm was detected\n");
		need_clear = 1;
	}

	//check for Periodic Timer Flag
	if (ctrl[1] & RX8010_BIT_FLAG_TF) {
		dev_warn(&client->dev, "Periodic timer was detected\n");
		need_clear = 1;
	}

	//check for Update Timer Flag
	if (ctrl[1] & RX8010_BIT_FLAG_UF) {
		dev_warn(&client->dev, "Update timer was detected\n");
		need_clear = 1;
	}

	//reset or clear needed?
	if (*need_reset || need_clear) {
		//clear flag register
		err = rx8010_write_reg(client, RX8010_REG_FLAG, 0x00);
		if (err)
			goto out;

		//clear ctrl register
		err = rx8010_write_reg(client, RX8010_REG_CTRL, 0x00);
		if (err)
			goto out;
	}
out:
	return err;
}

//----------------------------------------------------------------------
// rx8010_read_alarm()
// reads current Alarm
//
// Notes: - currently filters the AE bits (bit 7)
//        - assumes WADA setting is week (week/day)
//----------------------------------------------------------------------
static int rx8010_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	struct i2c_client *client = rx8010->client;
	u8 alarmvals[3];		//minute, hour, week/day values
	u8 ctrl[3];				//extension, flag, control values
	int err;

	if (client->irq <= 0)
		return -EINVAL;

	//get current minute, hour, week/day alarm values
	err = rx8010_read_regs(client, RX8010_REG_ALMIN, 3, alarmvals);
	if (err)
		return err;
	dev_dbg(dev, "%s: minutes:0x%02x hours:0x%02x week/day:0x%02x\n",
		__func__, alarmvals[0], alarmvals[1], alarmvals[2]);


	//get current extension, flag, control register values
	err = rx8010_read_regs(client, RX8010_REG_EXT, 3, ctrl);
	if (err)
		return err;
	dev_dbg(dev, "%s: extension:0x%02x flag:0x%02x control:0x%02x \n",
		__func__, ctrl[0], ctrl[1], ctrl[2]);

	// Hardware alarm precision is 1 minute
	t->time.tm_sec = 0;
	t->time.tm_min = bcd2bin(alarmvals[0] & 0x7f);		//0x7f filters AE bit currently
	t->time.tm_hour = bcd2bin(alarmvals[1] & 0x3f);		//0x3f filters AE bit currently, also 24hr only

	t->time.tm_wday = -1;
	t->time.tm_mday = -1;
	t->time.tm_mon = -1;
	t->time.tm_year = -1;

	dev_dbg(dev, "%s: date: %ds %dm %dh %dmd %dm %dy\n",
		__func__,
		t->time.tm_sec, t->time.tm_min, t->time.tm_hour,
		t->time.tm_mday, t->time.tm_mon, t->time.tm_year);

	t->enabled = !!(rx8010->ctrlreg & RX8010_BIT_CTRL_AIE);		//check if interrupt is enabled
	t->pending = (ctrl[1] & RX8010_BIT_FLAG_AF) && t->enabled;	//check if flag is triggered

	return err;
}

//----------------------------------------------------------------------
// rx8010_set_alarm()
// sets Alarm
//
// Notes: - currently filters the AE bits (bit 7)
//        - assumes WADA setting is week (week/day)
//----------------------------------------------------------------------
static int rx8010_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 alarmvals[3];		//minute, hour, day
	u8 extreg;				//extension register
	u8 flagreg;				//flag register
	int err;

	if (client->irq <= 0)
		return -EINVAL;

	//get current extension register
	err = rx8010_read_reg(client, RX8010_REG_EXT, &extreg);
	if (err <0)
		return err;

	//get current flag register
	err = rx8010_read_reg(client, RX8010_REG_FLAG, &flagreg);
	if (err <0)
		return err;

	// Hardware alarm precision is 1 minute
	alarmvals[0] = bin2bcd(t->time.tm_min);
	alarmvals[1] = bin2bcd(t->time.tm_hour);
	alarmvals[2] = bin2bcd(t->time.tm_mday);
	dev_dbg(dev, "%s: write 0x%02x 0x%02x 0x%02x\n", __func__, alarmvals[0], alarmvals[1], alarmvals[2]);

	//check interrupt enable and disable
	if (rx8010->ctrlreg & RX8010_BIT_CTRL_AIE) {
		rx8010->ctrlreg &= ~RX8010_BIT_CTRL_AIE;
		err = rx8010_write_reg(rx8010->client, RX8010_REG_CTRL, rx8010->ctrlreg);
		if (err)
			return err;
	}

	//write the new minute and hour values
	//Note:assume minute and hour values will be enabled. Bit 7 of each of the
	//     minute, hour, week/day register can be set which will "disable" the
	//     register from triggering an alarm. See the RX8010 spec for more information
	err = rx8010_write_regs(rx8010->client, RX8010_REG_ALMIN, 2, alarmvals);
	if (err)
		return err;

	//set Week/Day bit
	// Week setting is typically not used, so we will assume "day" setting
	extreg |= RX8010_BIT_EXT_WADA;		//set to "day of month"
	err = rx8010_write_reg(rx8010->client, RX8010_REG_EXT, extreg);
	if (err)
		return err;

	//set Day of Month register
	if (alarmvals[2] == 0) {
		alarmvals[2] |= 0x80;	//turn on AE bit to ignore day of month (no zero day)
		err = rx8010_write_reg(rx8010->client, RX8010_REG_ALWDAY, alarmvals[2]);
	}
	else {
		err = rx8010_write_reg(rx8010->client, RX8010_REG_ALWDAY, alarmvals[2]);
	}
	if (err)
		return err;

	//clear Alarm Flag
	flagreg &= ~RX8010_BIT_FLAG_AF;
	err = rx8010_write_reg(rx8010->client, RX8010_REG_FLAG, flagreg);
	if (err)
		return err;

	//re-enable interrupt if required
	if (t->enabled) {
		rx8010->ctrlreg |= RX8010_BIT_CTRL_AIE;		//set alarm interrupt enable
		err = rx8010_write_reg(rx8010->client, RX8010_REG_CTRL, rx8010->ctrlreg);
		if (err)
			return err;
	}

	return 0;
}

//----------------------------------------------------------------------
// rx8010_alarm_irq_enable()
// sets enables Alarm IRQ
//
// Todo: -
//
//----------------------------------------------------------------------
static int rx8010_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 flagreg;
	u8 ctrl;
	int err;

	//get the current ctrl settings
	ctrl = rx8010->ctrlreg;

	if (enabled)
		ctrl |= RX8010_BIT_CTRL_AIE;		//set the AIE
	else
		ctrl &= ~RX8010_BIT_CTRL_AIE;		//clear the AIE

	//clear alarm flag
	err = rx8010_read_reg(client, RX8010_REG_FLAG, &flagreg);
	if (err <0)
		return err;
	flagreg &= ~RX8010_BIT_FLAG_AF;
	err = rx8010_write_reg(rx8010->client, RX8010_REG_FLAG, flagreg);
	if (err)
		return err;

	//update the Control register if the setting changed
	if (ctrl != rx8010->ctrlreg) {
		rx8010->ctrlreg = ctrl;
		err = rx8010_write_reg(rx8010->client, RX8010_REG_CTRL, rx8010->ctrlreg);
		if (err)
			return err;
	}
	return 0;
}

//----------------------------------------------------------------------
// rx8010_ioctl()
// ioctl routine for the rx8010 driver
// example of how ioctls would be implemented
//
// Note: this routine is included as an example and should be removed if
//       not implemented
//----------------------------------------------------------------------
static int rx8010_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);

	u8 ctrl[3];		//store Extension, Flag, Control regs
	int err = 0;

	//get current extension, flag, control register values
	err = rx8010_read_regs(rx8010->client, RX8010_REG_EXT, 3, ctrl);
	if (err)
		return err;

	//do some stuff to the registers based on the cmd
	switch (cmd) {
	case 0:
		//i.e. change setting;
		break;
	case 1:
		//i.e. clear flag
		break;
	default:
		err = -ENOIOCTLCMD;
	}

	//write back the modified registers
	err = rx8010_write_regs(rx8010->client, RX8010_REG_EXT, 3, ctrl);
	if (err)
		return err;

	return err;
}

static struct rtc_class_ops rx8010_rtc_ops = {
	.read_time = rx8010_get_time,
	.set_time = rx8010_set_time,
	.read_alarm = rx8010_read_alarm,
	.set_alarm = rx8010_set_alarm,
	.alarm_irq_enable = rx8010_alarm_irq_enable,
	.ioctl			= rx8010_ioctl,					//remove if ioctls are not implemented
};


//----------------------------------------------------------------------
// rx8010_probe()
// probe routine for the rx8010 driver
//
// Todo: - maybe change kzalloc to use devm_kzalloc
//       -
//----------------------------------------------------------------------
static int rx8010_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct rx8010_data *rx8010;
	int err, need_reset = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&adapter->dev, "doesn't support required functionality\n");
		err = -EIO;
		goto errout;
	}

	rx8010 = kzalloc(sizeof(*rx8010), GFP_KERNEL);
	if (!rx8010) {
		dev_err(&adapter->dev, "failed to alloc memory\n");
		err = -ENOMEM;
		goto errout;
	}

	rx8010->client = client;
	i2c_set_clientdata(client, rx8010);
	INIT_WORK(&rx8010->work, rx8010_work);

	device_init_wakeup(&client->dev, 1);
	err = rx8010_init_client(client, &need_reset);
	if (err)
		goto errout_free;

	if (need_reset) {
		struct rtc_time tm;
		dev_info(&client->dev, "bad conditions detected, resetting date\n");
		rtc_time_to_tm(0, &tm);		// set to 1970/1/1
		rx8010_set_time(&client->dev, &tm);
	}

	rx8010->rtc = rtc_device_register(client->name, &client->dev, &rx8010_rtc_ops, THIS_MODULE);
	if (IS_ERR(rx8010->rtc)) {
		err = PTR_ERR(rx8010->rtc);
		dev_err(&client->dev, "unable to register the class device\n");
		goto errout_free;
	}

	if (client->irq > 0) {
		dev_info(&client->dev, "IRQ %d supplied\n", client->irq);
		err = request_irq(client->irq, rx8010_irq, 0, "rx8010", client);
		if (err) {
			dev_err(&client->dev, "unable to request IRQ\n");
			goto errout_reg;
		}
	}

	rx8010->rtc->irq_freq = 1;
	rx8010->rtc->max_user_freq = 1;

	return 0;

//	if (client->irq > 0)
//		free_irq(client->irq, client);

errout_reg:
	rtc_device_unregister(rx8010->rtc);

errout_free:
	kfree(rx8010);

errout:
	dev_err(&adapter->dev, "probing for rx8010 failed\n");
	return err;
}



//----------------------------------------------------------------------
// rx8010_remove()
// remove routine for the rx8010 driver
//
// Todo: - maybe change kzalloc to devm_kzalloc
//       -
//----------------------------------------------------------------------
static int rx8010_remove(struct i2c_client *client)
{
	struct rx8010_data *rx8010 = i2c_get_clientdata(client);
	struct mutex *lock = &rx8010->rtc->ops_lock;

	if (client->irq > 0) {
		mutex_lock(lock);
		rx8010->exiting = 1;
		mutex_unlock(lock);

		free_irq(client->irq, client);
		cancel_work_sync(&rx8010->work);
	}

	rtc_device_unregister(rx8010->rtc);
	kfree(rx8010);
	return 0;
}

static struct i2c_driver rx8010_driver = {
	.driver = {
		.name = "rtc-rx8010",
		.owner = THIS_MODULE,
	},
	.probe		= rx8010_probe,
	.remove		= rx8010_remove,
	.id_table	= rx8010_id,
};

static int __init rx8010_init(void)
{
	return i2c_add_driver(&rx8010_driver);
}

static void __exit rx8010_exit(void)
{
	i2c_del_driver(&rx8010_driver);
}

module_init(rx8010_init);
module_exit(rx8010_exit);

MODULE_AUTHOR("Dennis Henderson <henderson.dennis@erd.epson.com>");
MODULE_DESCRIPTION("RX-8010 SJ RTC driver");
MODULE_LICENSE("GPL");
