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
portC	x	x	SCL	SDA	A-M	V-M	A+M	V+M		I2C(PC5:�N���b�N PC4:�f�[�^) ADC(PC3 ~ 0 �d���d���v)
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

/*/////////////////////////////////////////////////////////////////////////////
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

// LCD�̏�����
void init_lcd()
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


int main(void)
{
	// �|�[�g�̏�����
	DDRD = _BV(2);			 // RST�s�����o�͂�
	PORTC = _BV(4) | _BV(5); // SCL, SDA�����v���A�b�v��L��
	
	i2c_init();				 // AVR����I2C���W���[���̏�����
	wait_ms(500);
	
	PORTD &= ~_BV(2);		 // RST��L�ɂ��܂��B���Z�b�g
	wait_ms(1);
	PORTD |= _BV(2);		 // RST��H�ɂ��܂��B���Z�b�g����
	wait_ms(10);
	
	// �f�o�C�X�̏�����
	init_lcd();
	
	// �P�s�ڂ̕\��
	i2c_puts("Dual Power");
	
	// �Q�s�ڂɃJ�[�\�����ړ�
	i2c_cmd(0b11000000);	// ADDR=0x40
	
	// �J�i�̕\���i���̃\�[�X�v���O������SJIS�ŋL�q����Ă��Ȃ��Ƃ��܂��\������܂���j
	i2c_puts("VA Meter 2");
	
    while(1)
    {
        //TODO:: Please write your application code 
		
    }
}
