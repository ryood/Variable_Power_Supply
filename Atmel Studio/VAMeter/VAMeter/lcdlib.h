#ifndef	_LCDLIB_H_
#define	_LCDLIB_H_

//SUNLIKE [SC1602]												|[SD1602]
//pin:	14	13	12	11	10	9	8	7	6	5	4	3	2	1	|16	15
//bit:	DB7	DB6	DB5	DB4	DB3	DB2	DB1	DB0	E	R/W	RS	Vo	VDD	VSS	|K	A
//		<- 4bit mode ->									+V	GND	|-	+


#define	F_CPU	1000000UL	//1MHz

#include	<avr/io.h>
#include	<util/delay.h>

//�g�p����|�[�g�i�P��̃|�[�g�j
#define	LCD_PORT	PORTD
#define	LCD_DDR		DDRD

//�|�[�g�̃r�b�g�̊��蓖�āi4bit���[�h���O��j
#define	LCD_FIXBITS	0	//LCD�̐���Ƃ͖��֌W�ȃr�b�g�������ɒ�`����i�������0�j
#define	LCD_RS		(1 << PD5)	//Register Select	H:data	L:command
#define	LCD_RW		0			//Read/Write		H:read	L:write		���g�p�����i�n�[�h�I��write�Œ�j
#define	LCD_E		(1 << PD6)	//Enable Signal		H->L:strobe
#define	LCD_DATABITS_SHIFT	0	//PD3-PD0	�iLSB�����牽�r�b�g�߂��f�[�^�r�b�g���B��Fbit3-0�Ȃ�0�Abit7-4�Ȃ�4�j
#define	LCD_DATABITS	(0x0F << LCD_DATABITS_SHIFT)
#define	LCD_DATABITS_REVERSE	0	//�f�[�^4bit�̐ڑ��Ή�	0:MSB=DB7�`LSB=DB4	1:MSB=DB4�`LSB=DB7

//�R�}���h
void	lcdInit();
void	lcdCmd(uint8_t bits);
void	lcdPutChar(char c);
void	lcdPutStr(const char* s);
void	lcdPutUInt(uint16_t n);
void	lcdPutInt(int16_t n);
void	lcdDefChar(uint8_t id, const uint8_t* dots);

#define	lcdCls()		lcdCmd(LCD_CMD_CLEAR_DISPLAY)
#define	lcdSetPos(x, y)	lcdCmd(LCD_CMD_SET_DDRAM_ADRS | ((y) * 0x40 + (x)))
#define	lcdBitReset()	LCD_PORT &= LCD_FIXBITS

//Instruction
#define	LCD_CMD_CLEAR_DISPLAY	0b00000001
#define	LCD_CMD_RETURN_HOME		0b00000010
#define	LCD_CMD_ENTRY_MODE		0b00000100
#define	LCD_CMD_DISPLAY_MODE	0b00001000
#define	LCD_CMD_CURSOR_MODE		0b00010000
#define	LCD_CMD_FUNCTION_SET	0b00100000
#define	LCD_CMD_SET_CGRAM_ADRS	0b01000000
#define	LCD_CMD_SET_DDRAM_ADRS	0b10000000

//Instruction�̃p�����[�^
//(LCD_CMD_AAA | LCD_SUBCMD_XXX1 | LCD_SUBCMD_XXX2 | �c)�̌`�Ŏg��

//LCD_CMD_ENTRY_MODE
#define	LCD_SUBCMD_INCREMENT	0b00000010	//���͈ʒu��i�߂�i���w�肾��Decrement�j
#define	LCD_SUBCMD_WITH_SHIFT	0b00000001	//�\���V�t�g����𔺂��iLCD_CMD_CURSOR_MODE���ݒ肷��j

//LCD_CMD_DISPLAY_MODE
#define	LCD_SUBCMD_SET_DISPLAY	0b00000100	//��ʕ\������i���w�肾�ƕ\�����e��ۂ����܂܉�ʂ������j
#define	LCD_SUBCMD_SET_CURSOR	0b00000010	//�J�[�\����\������i�A���_�[�o�[��\������j
#define	LCD_SUBCMD_CURSOR_BLINK	0b00000001	//�J�[�\����_�ł�����i�����ɔ킳���č��}�X���_�ł���j

//LCD_CMD_CURSOR_MODE
#define	LCD_SUBCMD_SHIFT_ON		0b00001000	//DDRAM�̕ύX�����ɕ\�����V�t�g������i���w�肾�ƃJ�[�\�����ړ�������j
#define	LCD_SUBCMD_RIGHT		0b00000100	//�E�փV�t�g������i���w�肾�ƍ��ցj����L�A�J�[�\���ړ����[�h���͈Ӗ��������Ȃ�

//LCD_CMD_FUNCTION_SET
#define	LCD_SUBCMD_DL8BIT		0b00010000	//Data Length 8-bit�i���w�肾��4-bit���[�h�j
#define	LCD_SUBCMD_2LINE		0b00001000	//�\����2�s�g�p����i���w�肾��1�s�̂ݎg�p�\�ɂȂ�j
#define	LCD_SUBCMD_FONT5X10		0b00000100	//Font Type 5x10�i���w�肾��5x8�j��SC1602/SD1602�͕����I�ɔ�Ή�


#endif	//_LCDLIB_H_
