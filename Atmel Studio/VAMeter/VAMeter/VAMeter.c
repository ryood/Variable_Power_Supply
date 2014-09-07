/*
 * VAMeter.c
 *
 * Created: 2014/09/07 11:03:43
 *  Author: gizmo
 */ 


//	ATmega88
//	LM317�g�p �ϓd�����i�ȈՓd���v�E�ȈՓd���v�t���j

/*
�|�[�g�Ή�	[ATmega88]	x:���݂��Ȃ�(PC6��RESET) -:��
bit		7	6	5	4	3	2	1	0
portB	-	-	-	-	-	-	-	CKO		PB0:�V�X�e���N���b�N�o��
portC	x	x	-	-	-	-	VM	AM		ADC(PC1:�d���v, PC0:�d���v)
portD	-	E	RS	-	DB7	DB6	DB5	DB4		LCD�ڑ�

�y�q���[�Y�r�b�g�z(avrsp.exe -rf)	���V�X�e���N���b�N���o�͂���ȊO�͍H��o�אݒ�
Low: 22 High: DF Ext: 07

Low: 00100010
     ||||++++-- CKSEL[3:0] �V�X�e���N���b�N�I��
     ||++-- SUT[1:0] �N������
     |+-- CKOUT (0:PB0�ɃV�X�e���N���b�N���o��)
     +-- CKDIV8 �N���b�N���������l (1:1/1, 0:1/8)

High:11-11111
     |||||+++-- BODLEVEL[2:0] (111:��, 110:1.8V, 101:2.7V, 100:4.3V)
     ||||+-- EESAVE (������EEPROM�� 1:����, 0:�ێ�)
     |||+-- WDTON (1:WDT�ʏ퓮��, 0:WDT�펞ON)
     ||+-- SPIEN (1:ISP�֎~, 0:ISP����) ��Parallel���̂�
     |+-- DWEN (On-Chip�f�o�b�O 1:����, 0:�L��)
     +-- RSTDISBL (RESET�s�� 1:�L��, 0:����(PC6))

Ext: -----001
          ||+-- BOOTRST ���f�[�^�V�[�g�Q��
          ++-- BOOTSZ[1:0] ���f�[�^�V�[�g�Q��

�r���h��
WinAVR-20080610		�œK���I�v�V����:-Os	���w���C�u������ǉ�:libm.a
ATmega88	1MHz�i8MHz��8�����j

����
2011/04/17	v1.00	Program:    2498 bytes (30.5% Full)
					Data:         37 bytes (3.6% Full)
				�E����
*/


#include	<avr/io.h>
#include	<avr/sleep.h>
#include	<avr/interrupt.h>
#include	"lcdlib.h"		//F_CPU��`��<util/delay.h>���܂�

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


//�b�P�ʂ̎��ԑ҂�
void	delay_sec(uint8_t sec)
{
	uint8_t	i, m;
	for (i = 0; i < sec; i++)
		for (m = 0; m < 5; m++)
			_delay_ms(200);
}

//LCD�̕\����1�s��������
void	lcdLineClear(uint8_t y)
{
	lcdSetPos(0, y);
	lcdPutStr("                ");	//16��
}

//�d����\������
//�����F	mV�P�ʂ̒l
void	lcdPutVoltage(uint16_t mValue)
{
	uint16_t	num, dec;
	
	lcdLineClear(VOLTAGE_Y);
	lcdSetPos(VOLTAGE_X, VOLTAGE_Y);
	
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
	
	lcdPutStr(" V");
}

//�d����\������
//�����F	0.1mA�P�ʂ̒l
void	lcdPutCurrent(uint16_t mValue)
{
	uint16_t	num, dec;
	
	lcdLineClear(CURRENT_Y);
	lcdSetPos(CURRENT_X, CURRENT_Y);
	
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
	
	lcdPutStr(" mA");
}

//�o�b�t�@�̒l�����v����
uint16_t	total(uint16_t values[])
{
	uint8_t	i;
	uint16_t	n = 0UL;
	for (i = 0; i < BUFSIZE; i++) n += values[i];
	return n;
}

//A/D�ϊ��X���[�v����̕��A
ISR(ADC_vect)
{
	//�����Ȃ�
}

//ADMUX�ɐݒ肷���d���Ɠ��̓s���̓��e
//REFS0 -> 00:AREF, 01:AVCC, 11:�����1.1V
//MUX0  -> 0111-0000:PC7-PC0, 1110:�����I��1.1V��ڑ�, 1111:�����I��0V��ڑ�
#define	ADC_SET_CURRENT	((0b00 << REFS0) | (0b0000 << MUX0))
#define	ADC_SET_VOLTAGE	((0b00 << REFS0) | (0b0001 << MUX0))

int	main(void)
{
	uint16_t	adcVoltages[BUFSIZE] = {0UL};
	uint16_t	adcCurrents[BUFSIZE] = {0UL};
	uint16_t	adcValue;
	uint16_t	mVoltage, mCurrent;
	uint8_t	idx;
	float	fv, fa;
	
	lcdInit();	//LCD������
	
	//LCD�����\��
	lcdSetPos(0, VOLTAGE_Y);	lcdPutStr("Voltage");
	lcdSetPos(0, CURRENT_Y);	lcdPutStr("Current");
	delay_sec(3);
	
	//�m�C�Y�ጸ�����҂���ADC�̃|�[�g���v���A�b�v���Ă���
	DDRC	= 0b00000000;	//portC�S�����͕���
	PORTC	= ~((1 << PC0) | (1 << PC1));	//�g�p����ADC���̓s���ȊO���v���A�b�v
	
	//����d�͍팸�̂���ADC�g�p�s�����f�W�^�����͋֎~�ɂ���iADC���́i�A�i���O�j��p�ƂȂ�j
	DIDR0 |= (1 << ADC0D) | (1 << ADC1D);	//PC0,PC1
	
	//ADC����ݒ�
	ADCSRA = 0b10000100		//bit2-0: 100 = 16����	1MHz/16=62.5kHz (50k�`200kHz�ł��邱��)
		| (1 << ADIE);		//���荞�݋��i�X���[�v�Ή��j
	
	//ADC�̊�d���Ɠ��̓s����ݒ肷��
	ADMUX =	ADC_SET_VOLTAGE;
	
	//A/D�ϊ��m�C�Y�ጸ�̃X���[�v���[�h��ݒ肷��
	set_sleep_mode(SLEEP_MODE_ADC);
	sei();			//���荞�݋���
	sleep_mode();	//A/D�R���o�[�^�������Ƃ��ď���ϊ��J�n�c�ϊ����c�ϊ�����
	adcValue = ADC;	//�l�̓ǂݎ̂�
	
	idx = 0;
	while(1)
	{
		//===== �d���𑪒肷�� =====
		ADMUX =	ADC_SET_VOLTAGE;
		_delay_ms(10);	//����҂�
		sleep_mode();	//�X���[�v���[�h�˓��c�ϊ����c�ϊ�����
		
		adcVoltages[idx] = ADC;
		adcValue = total(adcVoltages);
		
		//A/D�ϊ��l����d�������߂�
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV�P��
		fv *= (F_VMETER_RA + F_VMETER_RB) / F_VMETER_RA;	//�������Ă���l����{���̓d�������߂�
															//������fv=F_VREF�Ƃ����l������\�ȏ���l�ł���
		//�␳
		//�e�X�^�[�ł̎����l�Ɣ�r���āA�v�Z�l��F_VREF�𒆐S�ɖ�1������Ă����̂ŕ␳����B
		//�E�v�Z�l��F_VREF���傫���Ƃ��A���̒l�͎����l����1���傫�������B
		//�E�v�Z�l��F_VREF��菬�����Ƃ��A���̒l�͎����l����1�������������B
		fv -= (fv - F_VREF * 1000) * 0.01;	//fv��mV�P�ʂł��邱�Ƃɒ���
		
		//�d����\������
		mVoltage = (uint16_t)fv;
		lcdPutVoltage(mVoltage);
		
		//===== �d���𑪒肷�� =====
		ADMUX =	ADC_SET_CURRENT;
		_delay_ms(10);	//����҂�
		sleep_mode();	//�X���[�v���[�h�˓��c�ϊ����c�ϊ�����
		
		adcCurrents[idx] = ADC;
		adcValue = total(adcCurrents);
		
		//A/D�ϊ��l����d�������߁A�d�����Z�o����
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV�P��
		fa = fv / F_SHUNT_R;	//mA�P��
		
		//�d����\������
		mCurrent = (uint16_t)(fa * 10.0);	//0.1mA�P��
		lcdPutCurrent(mCurrent);
		
		//���̃��[�v�̏���
		if (++idx == BUFSIZE) idx = 0;
	}
	
	return 0;
}