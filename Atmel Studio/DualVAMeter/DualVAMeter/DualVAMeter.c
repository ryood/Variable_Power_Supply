/*
 * DualVAMeter.c
 *
 * Created: 2014/10/16 19:57:06
 *  Author: gizmo
 */ 


//	ATmega329P
//	LM317/LM337使用 可変電圧源（簡易電圧計・簡易電流計付き）

/*
ポート対応	[ATmega88]	x:存在しない(PC6はRESET) -:空き
bit		7	6	5	4	3	2	1	0
portB	-	-	-	-	-	-	-	CKO		PB0:システムクロック出力
portC	x	x	SCL	SDA	A-M	V-M	A+M	V+M		I2C(PC5:クロック PC4:データ) ADC(PC3 ~ 0 電流電圧計)
portD	-	-	-	-	-	RST	-	-		LCD		

【ヒューズビット】 8MHzクロック システムクロック出力
Low: A2 High: D9 Ext: 0F

ビルド環境
AtmelStudio 6.1		最適化オプション:-Os	数学ライブラリを追加:libm.a
ATmega328P	8MHz（内蔵RC）

履歴
2014/10/16	by gizmo
*/

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/signal.h>
#include <compat/twi.h>

// コントラストの設定
unsigned char contrast = 0b110000;	// 3.0V時 数値を上げると濃くなります。
									// 2.7Vでは0b111000くらいにしてください。。
									// コントラストは電源電圧，温度によりかなり変化します。実際の液晶をみて調整してください。
										
// この関数でμ秒のウェイトを入れます。引数 100 = 100μ秒ウェイト
// クロックスピードにより調整してください。
static void wait_us(short t)
{
	while(t-->=0){
		asm volatile ("nop");
	}
}

// この関数でミリ秒のウェイトを入れます。引数 100 = 100ミリ秒ウェイト
// クロックスピードにより調整してください。
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
	wait_us(50);	// １命令ごとに余裕を見て50usウェイトします。
	
	return rv;
}

// コマンドを送信します。HD44780でいうRS=0に相当
void i2c_cmd(unsigned char db)
{
	i2c_write(0b01111100, 0b00000000, db);
}

// データを送信します。HD44780でいうRS=1に相当
void i2c_data(unsigned char db)
{
	i2c_write(0b01111100, 0b01000000, db);
}

// （主に）文字列を連続送信します。
void i2c_puts(unsigned char *s)
{
	while(*s){
		i2c_data(*s++);
	}
}

// LCDの初期化
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
	wait_ms(2);			 // Clear Displayは追加ウェイトが必要
}


int main(void)
{
	// ポートの初期化
	DDRD = _BV(2);			 // RSTピンを出力に
	PORTC = _BV(4) | _BV(5); // SCL, SDA内蔵プルアップを有効
	
	i2c_init();				 // AVR内蔵I2Cモジュールの初期化
	wait_ms(500);
	
	PORTD &= ~_BV(2);		 // RSTをLにします。リセット
	wait_ms(1);
	PORTD |= _BV(2);		 // RSTをHにします。リセット解除
	wait_ms(10);
	
	// デバイスの初期化
	init_lcd();
	
	// １行目の表示
	i2c_puts("Dual Power");
	
	// ２行目にカーソルを移動
	i2c_cmd(0b11000000);	// ADDR=0x40
	
	// カナの表示（このソースプログラムはSJISで記述されていないとうまく表示されません）
	i2c_puts("VA Meter 2");
	
    while(1)
    {
        //TODO:: Please write your application code 
		
    }
}
