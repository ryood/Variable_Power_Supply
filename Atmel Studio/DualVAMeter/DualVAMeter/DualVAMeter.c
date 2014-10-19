/*
 * DualVAMeter.c
 *
 * Created: 2014/10/16 19:57:06
 *  Author: gizmo
 */ 


//	ATmega329P
//	LM317/LM337�g�p �ϓd�����i�ȈՓd���v�E�ȈՓd���v�t���j

/*
�|�[�g�Ή�	[ATmega88]	x:���݂��Ȃ�(PC6��RESET) -:��
bit		7	6	5	4	3	2	1	0
portB	-	-	-	-	-	-	-	CKO		PB0:�V�X�e���N���b�N�o��
portC	x	x	SCL	SDA	A-M	V-M	A+M	V+M		I2C(PC5:�N���b�N PC4:�f�[�^) ADC(PC3..0 �d���d���v)
portD	-	-	-	-	-	RST	-	-		LCD		

�y�q���[�Y�r�b�g�z 8MHz�N���b�N �V�X�e���N���b�N�o��
Low: A2 High: D9 Ext: 0F

�r���h��
AtmelStudio 6.1		�œK���I�v�V����:-Os	���w���C�u������ǉ�:libm.a
ATmega328P	8MHz�i����RC�j

����
2014/10/16	by gizmo
*/

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/signal.h>
#include <compat/twi.h>
#include <avr/sleep.h>

//Vref(V)
#define	F_VREF	2.78	//�V�����g���M�����[�^

//�d������p�̃V�����g��R(��)
#define	F_SHUNT_R	9.9

//�d������p�̕�����R(k��)
#define	F_VMETER_RA	 99.5	//ADC���͓_����GND��
#define	F_VMETER_RB	298.0	//ADC���͓_�����H��

//�o�b�t�@�T�C�Y
//total()�̌��ʂ��I�[�o�[�t���[���Ȃ��悤60�ȉ��ł��邱�Ɓi10�ŏ\���j
#define	BUFSIZE	10

//�\���ʒu
#define	VOLTAGE_X	0
#define	VOLTAGE_Y	0
#define	CURRENT_X	0
#define	CURRENT_Y	1

// �R���g���X�g�̐ݒ�
unsigned char contrast = 0b110000;	// 3.0V�� ���l���グ��ƔZ���Ȃ�܂��B
									// 2.7V�ł�0b111000���炢�ɂ��Ă��������B�B
									// �R���g���X�g�͓d���d���C���x�ɂ�肩�Ȃ�ω����܂��B���ۂ̉t�����݂Ē������Ă��������B
										

// ���̊֐��Ńʕb�̃E�F�C�g�����܂��B���� 100 = 100�ʕb�E�F�C�g
// �N���b�N�X�s�[�h�ɂ�蒲�����Ă��������B
static void wait_us(short t)
{
	while(t-->=0){
		asm volatile ("nop");
	}
}

// ���̊֐��Ń~���b�̃E�F�C�g�����܂��B���� 100 = 100�~���b�E�F�C�g
// �N���b�N�X�s�[�h�ɂ�蒲�����Ă��������B
static void wait_ms(short t)
{
	while(t-->=0){
		wait_us(1000);
	}
}

static void wait_sec(uint8_t sec)
{
	uint8_t	i, m;
	for (i = 0; i < sec; i++)
	for (m = 0; m < 5; m++)
	wait_ms(200);
}

/*/////////////////////////////////////////////////////////////////////////////
 *
 * I2C LCD
 *
 */

void i2c_init()
{
	// TWBR = {(CLOCK(8MHz) / I2C_CLK) - 16} / 2;
	// I2C_CLK = 100kHz, CLOCK = 8MHz, TWBR = 32
	// I2C_CLK = 100kHz, CLOCK = 20MHz, TWBR = 92
	TWBR = 32;
	TWSR = 0;
}

unsigned char wait_stat()
{
	while(!(TWCR & _BV(TWINT)));
	
	return TW_STATUS;
}

void i2c_stop()
{
	
	TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);
	while(TWCR & _BV(TWSTO));
}

unsigned char i2c_start(unsigned char addr, unsigned char eeaddr)
{
i2c_restart:
i2c_start_retry:
	TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
	switch(wait_stat()){
		case TW_REP_START:
		case TW_START:
			break;
		case TW_MT_ARB_LOST:
			goto i2c_start_retry;
		default:
			return 0;
	}
	TWDR = addr | TW_WRITE;
	TWCR = _BV(TWINT) | _BV(TWEN);
	switch(wait_stat()){
		case TW_MT_SLA_ACK:
			break;
		case TW_MT_SLA_NACK:
			goto i2c_restart;
		case TW_MT_ARB_LOST:
			goto i2c_start_retry;
		default:
			return 0;
	}
	TWDR = eeaddr;
	TWCR = _BV(TWINT) | _BV(TWEN);
	switch(wait_stat()){
		case TW_MT_DATA_ACK:
			break;
		case TW_MT_DATA_NACK:
			i2c_stop();
			return 0;
		case TW_MT_ARB_LOST:
			goto i2c_start_retry;
		default:
			return 0;
	}
	return 1;
}

unsigned char i2c_write(unsigned addr, unsigned char eeaddr, unsigned char dat)
{
	unsigned char rv=0;

restart:
begin:
	if(!i2c_start(addr, eeaddr))	goto quit;
	
	TWDR = dat;
	TWCR = _BV(TWINT) | _BV(TWEN);
	switch(wait_stat()){
		case TW_MT_DATA_ACK:
			break;
		case TW_MT_ARB_LOST:
			goto begin;
		case TW_MT_DATA_NACK:
		default:
			goto quit;
	}
	rv = 1;
quit:
	i2c_stop();
	wait_us(50);	// �P���߂��Ƃɗ]�T������50us�E�F�C�g���܂��B
	
	return rv;
}

// �R�}���h�𑗐M���܂��BHD44780�ł���RS=0�ɑ���
void i2c_cmd(unsigned char db)
{
	i2c_write(0b01111100, 0b00000000, db);
}

// �f�[�^�𑗐M���܂��BHD44780�ł���RS=1�ɑ���
void i2c_data(unsigned char db)
{
	i2c_write(0b01111100, 0b01000000, db);
}

// �i��Ɂj�������A�����M���܂��B
void i2c_puts(unsigned char *s)
{
	while(*s){
		i2c_data(*s++);
	}
}

/*/////////////////////////////////////////////////////////////////////////////
 *
 * LCD
 *
 */

// LCD�̏�����
void lcdInit()
{
	wait_ms(40);
	i2c_cmd(0b00111000); // function set
	i2c_cmd(0b00111001); // function set
	i2c_cmd(0b00010100); // interval osc
	i2c_cmd(0b01110000 | (contrast & 0xF)); // contrast Low
	
	i2c_cmd(0b01011100 | ((contrast >> 4) & 0x3)); // contast High/icon/power
	i2c_cmd(0b01101100); // follower control
	wait_ms(300);

	i2c_cmd(0b00111000); // function set
	i2c_cmd(0b00001100); // Display On
	
	i2c_cmd(0b00000001); // Clear Display
	wait_ms(2);			 // Clear Display�͒ǉ��E�F�C�g���K�v
}

#define lcdSetPos(x,y)	i2c_cmd(0b10000000+40*(y)+x)
#define lcdPutChar(c)	i2c_data(c)
#define lcdPutStr(s)	i2c_puts(s)

static	uint8_t	kbuf[5] = {0};	//16bit����5����	lcdPutUInt()�Ŏg�p
	
void	lcdPutUInt(uint16_t n)
{
	uint8_t	i = 0;
	
	do
	{
		kbuf[i++] = n % 10;
		n /= 10;
	}
	while (0 < n);
	
	while (i != 0)
	lcdPutChar('0' + kbuf[--i]);
}

/*/////////////////////////////////////////////////////////////////////////////
 *
 * �d���E�d���̕\��
 *
 */

//�d����\������
//�����F	mV�P�ʂ̒l
void	lcdPutVoltage(uint16_t mValue, int pos)
{
	uint16_t	num, dec;

	lcdSetPos(VOLTAGE_X + pos*8, VOLTAGE_Y);	
	if (pos == 1)
		lcdPutChar('-');
	
	num = mValue / 1000;	//������
	dec = mValue % 1000;	//������
	
	//10mV�����͏�����2�ʂ܂ŕ\���i������3�ʈȉ��͐؂�̂āj
	//10mV�ȏ�͏�����1�ʂ܂ŕ\���i������2�ʈȉ��͐؂�̂āj
	//[9.99][10.0]mV������
	if (num < 10)
	{
		lcdPutChar(' ');
		lcdPutUInt(num);
		lcdPutChar('.');
		//������3����2���\���i3���ڂ͐؂�̂āj
		dec /= 10;
		if (dec < 10) lcdPutChar('0');
			lcdPutUInt(dec);
	}
	else
	{
		lcdPutUInt(num);
		lcdPutChar('.');
		//������3����1���\���i2���ڈȍ~�͐؂�̂āj
		lcdPutUInt(dec / 1000);
	}
	
	lcdPutStr("V");
}

//�d����\������
//�����F	0.1mA�P�ʂ̒l
void	lcdPutCurrent(uint16_t mValue, int pos)
{
	uint16_t	num, dec;
	
	lcdSetPos(CURRENT_X + pos*8, CURRENT_Y);
	if (pos == 1)
		lcdPutChar('-');
	
	num = mValue / 10;	//������
	dec = mValue % 10;	//������
	
	//100mA�����͏�����1�ʂ܂ŕ\��
	//100mA�ȏ�͐������̂ݕ\���i�����_�ȉ��͐؂�̂āj
	//[99.9][100]mA������
	if (num < 100)
	{
		if (num < 10) lcdPutChar(' ');
			lcdPutUInt(num);
		lcdPutChar('.');
		lcdPutUInt(dec);
	}
	else
	{
		if (num < 1000) lcdPutChar(' ');
			lcdPutUInt(num);
	}
	
	lcdPutStr("mA");
}

//�o�b�t�@�̒l�����v����
uint16_t	total(uint16_t values[])
{
	uint8_t	i;
	uint16_t	n = 0UL;
	for (i = 0; i < BUFSIZE; i++) n += values[i];
	return n;
}

//ADMUX�ɐݒ肷���d���Ɠ��̓s���̓��e
//REFS0 -> 00:AREF, 01:AVCC, 11:�����1.1V
//MUX0  -> 0111-0000:PC7-PC0, 1110:�����I��1.1V��ڑ�, 1111:�����I��0V��ڑ�
#define	ADC_SET_PVOLTAGE	((0b01 << REFS0) | (0b0000 << MUX0))
#define	ADC_SET_PCURRENT	((0b01 << REFS0) | (0b0001 << MUX0))
#define	ADC_SET_NVOLTAGE	((0b01 << REFS0) | (0b0010 << MUX0))
#define	ADC_SET_NCURRENT	((0b01 << REFS0) | (0b0011 << MUX0))

int main(void)
{
	uint16_t	adcPVoltages[BUFSIZE] = {0UL};
	uint16_t	adcPCurrents[BUFSIZE] = {0UL};
	uint16_t	adcNVoltages[BUFSIZE] = {0UL};
	uint16_t	adcNCurrents[BUFSIZE] = {0UL};
	uint16_t	adcValue;
	uint16_t	mVoltage, mCurrent;
	uint8_t	idx;
	uint8_t	idx2;
	float	fv, fa;
		
	// �|�[�g�̏�����
	DDRD = _BV(2);			 // RST�s�����o�͂�
	PORTC = _BV(4) | _BV(5); // SCL, SDA�����v���A�b�v��L��
	
	
	// �f�o�C�X�̏�����
	i2c_init();				 // AVR����I2C���W���[���̏�����
	wait_ms(500);
	
	PORTD &= ~_BV(2);		 // RST��L�ɂ��܂��B���Z�b�g
	wait_ms(1);
	PORTD |= _BV(2);		 // RST��H�ɂ��܂��B���Z�b�g����
	wait_ms(10);
	
	lcdInit();
	
	//LCD�����\��
	lcdSetPos(0, VOLTAGE_Y);	lcdPutStr("Voltage @@");
	lcdSetPos(0, CURRENT_Y);	lcdPutStr("Current @@");
	wait_sec(3);
	
	//ADC����ݒ�
	ADCSRA = 0b10000110		//bit2-0: 101 = 64����	8MHz/64=125kHz (50k�`200kHz�ł��邱��)
		| (1 << ADIE);		//���荞�݋��i�X���[�v�Ή��j
	
	//ADC�̊�d���Ɠ��̓s����ݒ肷��
	ADMUX =	ADC_SET_PVOLTAGE;

/*	
	//A/D�ϊ��m�C�Y�ጸ�̃X���[�v���[�h��ݒ肷��
	set_sleep_mode(SLEEP_MODE_ADC);
	sei();			//���荞�݋���
	sleep_mode();	//A/D�R���o�[�^�������Ƃ��ď���ϊ��J�n�c�ϊ����c�ϊ�����
	adcValue = ADC;	//�l�̓ǂݎ̂�
*/

	lcdPutVoltage(15000, 0);
	lcdPutCurrent(5000, 0);
	lcdPutVoltage(15000, 1);
	lcdPutCurrent(5000, 1);
	
	idx = 0;
	idx2 = 0;
	
    while(1)
    {
#if 0
        //===== ���d��         ==============================================================================
		//
		
		//===== �d���𑪒肷�� =====
		ADMUX =	ADC_SET_PVOLTAGE;
		wait_ms(5);	//����҂�
		sleep_mode();	//�X���[�v���[�h�˓��c�ϊ����c�ϊ�����
			
		adcPVoltages[idx] = ADC;
		adcValue = total(adcPVoltages);
			
		//A/D�ϊ��l����d�������߂�
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV�P��
		fv *= (F_VMETER_RA + F_VMETER_RB) / F_VMETER_RA;	//�������Ă���l����{���̓d�������߂�
		//������fv=F_VREF�Ƃ����l������\�ȏ���l�ł���
		//�␳
		//�e�X�^�[�ł̎����l�Ɣ�r���āA�v�Z�l��F_VREF�𒆐S�ɖ�1������Ă����̂ŕ␳����B
		//�E�v�Z�l��F_VREF���傫���Ƃ��A���̒l�͎����l����1���傫�������B
		//�E�v�Z�l��F_VREF��菬�����Ƃ��A���̒l�͎����l����1�������������B
		//fv -= (fv - F_VREF * 1000) * 0.01;	//fv��mV�P�ʂł��邱�Ƃɒ���
			
		//�d����\������
		mVoltage = (uint16_t)fv;
		lcdPutVoltage(mVoltage, 0);
					
		//===== �d���𑪒肷�� =====
		ADMUX =	ADC_SET_PCURRENT;
		wait_ms(5);	//����҂�
		sleep_mode();	//�X���[�v���[�h�˓��c�ϊ����c�ϊ�����
			
		adcPCurrents[idx] = ADC;
		adcValue = total(adcPCurrents);
			
		//A/D�ϊ��l����d�������߁A�d�����Z�o����
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV�P��
		fa = fv / F_SHUNT_R;	//mA�P��
			
		//�d����\������
		mCurrent = (uint16_t)(fa * 10.0);	//0.1mA�P��
		lcdPutCurrent(mCurrent, 0);
			
		//���̃��[�v�̏���
		if (++idx == BUFSIZE) idx = 0;
			
		//===== ���d��         =============================================================================
		//
		
		//===== �d���𑪒肷�� =====
		ADMUX =	ADC_SET_NVOLTAGE;
		wait_ms(5);	//����҂�
		sleep_mode();	//�X���[�v���[�h�˓��c�ϊ����c�ϊ�����
			
		adcNVoltages[idx2] = ADC;
		adcValue = total(adcNVoltages);
			
		//A/D�ϊ��l����d�������߂�
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV�P��
		fv *= (F_VMETER_RA + F_VMETER_RB) / F_VMETER_RA;	//�������Ă���l����{���̓d�������߂�
		//������fv=F_VREF�Ƃ����l������\�ȏ���l�ł���
		//�␳
		//�e�X�^�[�ł̎����l�Ɣ�r���āA�v�Z�l��F_VREF�𒆐S�ɖ�1������Ă����̂ŕ␳����B
		//�E�v�Z�l��F_VREF���傫���Ƃ��A���̒l�͎����l����1���傫�������B
		//�E�v�Z�l��F_VREF��菬�����Ƃ��A���̒l�͎����l����1�������������B
		//fv -= (fv - F_VREF * 1000) * 0.01;	//fv��mV�P�ʂł��邱�Ƃɒ���
			
		//�d����\������
		mVoltage = (uint16_t)fv;
		lcdPutVoltage(mVoltage, 1);
			
		//===== �d���𑪒肷�� =====
		ADMUX =	ADC_SET_NCURRENT;
		wait_ms(5);	//����҂�
		sleep_mode();	//�X���[�v���[�h�˓��c�ϊ����c�ϊ�����
			
		adcNCurrents[idx2] = ADC;
		adcValue = total(adcNCurrents);
			
		//A/D�ϊ��l����d�������߁A�d�����Z�o����
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV�P��
		fa = fv / F_SHUNT_R;	//mA�P��
			
		//�d����\������
		mCurrent = (uint16_t)(fa * 10.0);	//0.1mA�P��
		lcdPutCurrent(mCurrent, 1);
			
		//���̃��[�v�̏���
		if (++idx2 == BUFSIZE) idx2 = 0;
#endif
    }
	
	return 0;
}
