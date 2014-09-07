#ifndef	_LCDLIB_H_
#define	_LCDLIB_H_

//SUNLIKE [SC1602]												|[SD1602]
//pin:	14	13	12	11	10	9	8	7	6	5	4	3	2	1	|16	15
//bit:	DB7	DB6	DB5	DB4	DB3	DB2	DB1	DB0	E	R/W	RS	Vo	VDD	VSS	|K	A
//		<- 4bit mode ->									+V	GND	|-	+


#define	F_CPU	1000000UL	//1MHz

#include	<avr/io.h>
#include	<util/delay.h>

//使用するポート（単一のポート）
#define	LCD_PORT	PORTD
#define	LCD_DDR		DDRD

//ポートのビットの割り当て（4bitモードが前提）
#define	LCD_FIXBITS	0	//LCDの制御とは無関係なビットをここに定義する（無ければ0）
#define	LCD_RS		(1 << PD5)	//Register Select	H:data	L:command
#define	LCD_RW		0			//Read/Write		H:read	L:write		※使用せず（ハード的にwrite固定）
#define	LCD_E		(1 << PD6)	//Enable Signal		H->L:strobe
#define	LCD_DATABITS_SHIFT	0	//PD3-PD0	（LSB側から何ビットめがデータビットか。例：bit3-0なら0、bit7-4なら4）
#define	LCD_DATABITS	(0x0F << LCD_DATABITS_SHIFT)
#define	LCD_DATABITS_REVERSE	0	//データ4bitの接続対応	0:MSB=DB7～LSB=DB4	1:MSB=DB4～LSB=DB7

//コマンド
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

//Instructionのパラメータ
//(LCD_CMD_AAA | LCD_SUBCMD_XXX1 | LCD_SUBCMD_XXX2 | …)の形で使う

//LCD_CMD_ENTRY_MODE
#define	LCD_SUBCMD_INCREMENT	0b00000010	//入力位置を進める（無指定だとDecrement）
#define	LCD_SUBCMD_WITH_SHIFT	0b00000001	//表示シフト動作を伴う（LCD_CMD_CURSOR_MODEも設定する）

//LCD_CMD_DISPLAY_MODE
#define	LCD_SUBCMD_SET_DISPLAY	0b00000100	//画面表示する（無指定だと表示内容を保ったまま画面を消す）
#define	LCD_SUBCMD_SET_CURSOR	0b00000010	//カーソルを表示する（アンダーバーを表示する）
#define	LCD_SUBCMD_CURSOR_BLINK	0b00000001	//カーソルを点滅させる（文字に被さって黒マスが点滅する）

//LCD_CMD_CURSOR_MODE
#define	LCD_SUBCMD_SHIFT_ON		0b00001000	//DDRAMの変更無しに表示をシフトさせる（無指定だとカーソルを移動させる）
#define	LCD_SUBCMD_RIGHT		0b00000100	//右へシフトさせる（無指定だと左へ）※上記、カーソル移動モード時は意味を持たない

//LCD_CMD_FUNCTION_SET
#define	LCD_SUBCMD_DL8BIT		0b00010000	//Data Length 8-bit（無指定だと4-bitモード）
#define	LCD_SUBCMD_2LINE		0b00001000	//表示で2行使用する（無指定だと1行のみ使用可能になる）
#define	LCD_SUBCMD_FONT5X10		0b00000100	//Font Type 5x10（無指定だと5x8）※SC1602/SD1602は物理的に非対応


#endif	//_LCDLIB_H_
