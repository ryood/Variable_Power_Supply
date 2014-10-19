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
portC	x	x	SCL	SDA	A-M	V-M	A+M	V+M		I2C(PC5:クロック PC4:データ) ADC(PC3..0 電流電圧計)
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
#include <avr/sleep.h>

//Vref(V)
#define	F_VREF	2.78	//シャントレギュレータ

//電流測定用のシャント抵抗(Ω)
#define	F_SHUNT_R	9.9

//電圧測定用の分圧抵抗(kΩ)
#define	F_VMETER_RA	 99.5	//ADC入力点からGND側
#define	F_VMETER_RB	298.0	//ADC入力点から回路側

//バッファサイズ
//total()の結果がオーバーフローしないよう60以下であること（10で十分）
#define	BUFSIZE	10

//表示位置
#define	VOLTAGE_X	0
#define	VOLTAGE_Y	0
#define	CURRENT_X	0
#define	CURRENT_Y	1

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

/*/////////////////////////////////////////////////////////////////////////////
 *
 * LCD
 *
 */

// LCDの初期化
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
	wait_ms(2);			 // Clear Displayは追加ウェイトが必要
}

#define lcdSetPos(x,y)	i2c_cmd(0b10000000+40*(y)+x)
#define lcdPutChar(c)	i2c_data(c)
#define lcdPutStr(s)	i2c_puts(s)

static	uint8_t	kbuf[5] = {0};	//16bit整数5桁分	lcdPutUInt()で使用
	
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
 * 電圧・電流の表示
 *
 */

//電圧を表示する
//引数：	mV単位の値
void	lcdPutVoltage(uint16_t mValue, int pos)
{
	uint16_t	num, dec;

	lcdSetPos(VOLTAGE_X + pos*8, VOLTAGE_Y);	
	if (pos == 1)
		lcdPutChar('-');
	
	num = mValue / 1000;	//整数部
	dec = mValue % 1000;	//小数部
	
	//10mV未満は小数第2位まで表示（小数第3位以下は切り捨て）
	//10mV以上は小数第1位まで表示（小数第2位以下は切り捨て）
	//[9.99][10.0]mVが境目
	if (num < 10)
	{
		lcdPutChar(' ');
		lcdPutUInt(num);
		lcdPutChar('.');
		//小数部3桁中2桁表示（3桁目は切り捨て）
		dec /= 10;
		if (dec < 10) lcdPutChar('0');
			lcdPutUInt(dec);
	}
	else
	{
		lcdPutUInt(num);
		lcdPutChar('.');
		//小数部3桁中1桁表示（2桁目以降は切り捨て）
		lcdPutUInt(dec / 1000);
	}
	
	lcdPutStr("V");
}

//電流を表示する
//引数：	0.1mA単位の値
void	lcdPutCurrent(uint16_t mValue, int pos)
{
	uint16_t	num, dec;
	
	lcdSetPos(CURRENT_X + pos*8, CURRENT_Y);
	if (pos == 1)
		lcdPutChar('-');
	
	num = mValue / 10;	//整数部
	dec = mValue % 10;	//小数部
	
	//100mA未満は小数第1位まで表示
	//100mA以上は整数部のみ表示（小数点以下は切り捨て）
	//[99.9][100]mAが境目
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

//バッファの値を合計する
uint16_t	total(uint16_t values[])
{
	uint8_t	i;
	uint16_t	n = 0UL;
	for (i = 0; i < BUFSIZE; i++) n += values[i];
	return n;
}

//ADMUXに設定する基準電圧と入力ピンの内容
//REFS0 -> 00:AREF, 01:AVCC, 11:内部基準1.1V
//MUX0  -> 0111-0000:PC7-PC0, 1110:内部的に1.1Vを接続, 1111:内部的に0Vを接続
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
		
	// ポートの初期化
	DDRD = _BV(2);			 // RSTピンを出力に
	PORTC = _BV(4) | _BV(5); // SCL, SDA内蔵プルアップを有効
	
	
	// デバイスの初期化
	i2c_init();				 // AVR内蔵I2Cモジュールの初期化
	wait_ms(500);
	
	PORTD &= ~_BV(2);		 // RSTをLにします。リセット
	wait_ms(1);
	PORTD |= _BV(2);		 // RSTをHにします。リセット解除
	wait_ms(10);
	
	lcdInit();
	
	//LCD初期表示
	lcdSetPos(0, VOLTAGE_Y);	lcdPutStr("Voltage @@");
	lcdSetPos(0, CURRENT_Y);	lcdPutStr("Current @@");
	wait_sec(3);
	
	//ADC動作設定
	ADCSRA = 0b10000110		//bit2-0: 101 = 64分周	8MHz/64=125kHz (50k～200kHzであること)
		| (1 << ADIE);		//割り込み許可（スリープ対応）
	
	//ADCの基準電圧と入力ピンを設定する
	ADMUX =	ADC_SET_PVOLTAGE;

/*	
	//A/D変換ノイズ低減のスリープモードを設定する
	set_sleep_mode(SLEEP_MODE_ADC);
	sei();			//割り込み許可
	sleep_mode();	//A/Dコンバータ初期化として初回変換開始…変換中…変換完了
	adcValue = ADC;	//値の読み捨て
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
        //===== 正電源         ==============================================================================
		//
		
		//===== 電圧を測定する =====
		ADMUX =	ADC_SET_PVOLTAGE;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
			
		adcPVoltages[idx] = ADC;
		adcValue = total(adcPVoltages);
			
		//A/D変換値から電圧を求める
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV単位
		fv *= (F_VMETER_RA + F_VMETER_RB) / F_VMETER_RA;	//分圧している値から本来の電圧を求める
		//ここでfv=F_VREFとした値が測定可能な上限値である
		//補正
		//テスターでの実測値と比較して、計算値はF_VREFを中心に約1％ずれていたので補正する。
		//・計算値がF_VREFより大きいとき、その値は実測値より約1％大きかった。
		//・計算値がF_VREFより小さいとき、その値は実測値より約1％小さかった。
		//fv -= (fv - F_VREF * 1000) * 0.01;	//fvはmV単位であることに注意
			
		//電圧を表示する
		mVoltage = (uint16_t)fv;
		lcdPutVoltage(mVoltage, 0);
					
		//===== 電流を測定する =====
		ADMUX =	ADC_SET_PCURRENT;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
			
		adcPCurrents[idx] = ADC;
		adcValue = total(adcPCurrents);
			
		//A/D変換値から電圧を求め、電流を算出する
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV単位
		fa = fv / F_SHUNT_R;	//mA単位
			
		//電流を表示する
		mCurrent = (uint16_t)(fa * 10.0);	//0.1mA単位
		lcdPutCurrent(mCurrent, 0);
			
		//次のループの準備
		if (++idx == BUFSIZE) idx = 0;
			
		//===== 負電源         =============================================================================
		//
		
		//===== 電圧を測定する =====
		ADMUX =	ADC_SET_NVOLTAGE;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
			
		adcNVoltages[idx2] = ADC;
		adcValue = total(adcNVoltages);
			
		//A/D変換値から電圧を求める
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV単位
		fv *= (F_VMETER_RA + F_VMETER_RB) / F_VMETER_RA;	//分圧している値から本来の電圧を求める
		//ここでfv=F_VREFとした値が測定可能な上限値である
		//補正
		//テスターでの実測値と比較して、計算値はF_VREFを中心に約1％ずれていたので補正する。
		//・計算値がF_VREFより大きいとき、その値は実測値より約1％大きかった。
		//・計算値がF_VREFより小さいとき、その値は実測値より約1％小さかった。
		//fv -= (fv - F_VREF * 1000) * 0.01;	//fvはmV単位であることに注意
			
		//電圧を表示する
		mVoltage = (uint16_t)fv;
		lcdPutVoltage(mVoltage, 1);
			
		//===== 電流を測定する =====
		ADMUX =	ADC_SET_NCURRENT;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
			
		adcNCurrents[idx2] = ADC;
		adcValue = total(adcNCurrents);
			
		//A/D変換値から電圧を求め、電流を算出する
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV単位
		fa = fv / F_SHUNT_R;	//mA単位
			
		//電流を表示する
		mCurrent = (uint16_t)(fa * 10.0);	//0.1mA単位
		lcdPutCurrent(mCurrent, 1);
			
		//次のループの準備
		if (++idx2 == BUFSIZE) idx2 = 0;
#endif
    }
	
	return 0;
}
