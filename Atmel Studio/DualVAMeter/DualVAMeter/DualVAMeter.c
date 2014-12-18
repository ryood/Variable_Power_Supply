/*
 * DualVAMeter.c
 *
 * Created: 2014/10/16 19:57:06
 *  Author: gizmo
 */ 


//	ATmega328P
//	LM317/LM337使用 可変電圧源（簡易電圧計・簡易電流計付き）

/*
ポート対応	[ATmega328P]	x:存在しない(PC6はRESET) -:空き
bit		7	6	5	4	3	2	1	0
portB	-	-	-	-	-	-	-	CKO		PB0:システムクロック出力
portC	x	x	SCL	SDA	IM	V-M	GND	V+M		I2C(PC5:クロック PC4:データ) ADC(PC3..0 電流電圧計)
portD	-	-	-	-	-	RST	-	-		LCD		

【ヒューズビット】 8MHzクロック
Low: E2 High: D9 Ext: 07

ビルド環境
AtmelStudio 6.1		最適化オプション:-Os	数学ライブラリを追加:libm.a
ATmega328P	8MHz（内蔵RC）

履歴
2014/12/18  電流電圧値,GND値を表示するように変更
2014/10/19  テスト用に修正
2014/10/16	by gizmo
*/

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/signal.h>
#include <compat/twi.h>
#include <avr/sleep.h>

//VCC(V)
#define	F_VCC	5.09	//AVCC

//電流測定用のシャント抵抗(Ω)
#define	F_SHUNT_R	0.47

//電圧測定用の分圧抵抗(kΩ)
#define	F_VMETER_POSITIVE_RA	1.995	//ADC入力点からGND側
#define	F_VMETER_POSITIVE_RB	9.956	//ADC入力点から回路側
#define	F_VMETER_NEGATIVE_RA	1.997	//ADC入力点からGND側
#define	F_VMETER_NEGATIVE_RB	9.909	//ADC入力点から回路側

//電圧測定用の分圧比
#define F_POSITIVE_V_RATE	(F_VMETER_POSITIVE_RA/(F_VMETER_POSITIVE_RA+F_VMETER_POSITIVE_RB))
#define F_NEGATIVE_V_RATE	(F_VMETER_NEGATIVE_RA/(F_VMETER_NEGATIVE_RA+F_VMETER_NEGATIVE_RB))

//電流値増幅用の抵抗(kΩ)
#define F_CURRENT_AMP_RA	0.9891	//オペアンプの反転入力のGND側
#define F_CURRENT_AMP_RB	9.886	//オペアンプの反転入力の出力側

//電流値の増幅率
#define F_CURRENT_AMP	((F_CURRENT_AMP_RA+F_CURRENT_AMP_RB)/F_CURRENT_AMP_RA)

//バッファサイズ
//total()の結果がオーバーフローしないよう60以下であること（10で十分）
#define	BUFSIZE	10

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

#define lcdSetPos(x,y)	i2c_cmd(0b10000000+0x40*(y)+(x))
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

//LCDの表示をクリア
void lcdClear()
{
	i2c_cmd(0b00000001); // Clear Display
	wait_ms(2);			 // Clear Displayは追加ウェイトが必要	
}

//LCDの表示を1行消去する
void	lcdLineClear(int x, int y)
{
	lcdSetPos(x * 8, y);
	lcdPutStr("        ");	//8桁
}

/*/////////////////////////////////////////////////////////////////////////////
 *
 * 電圧・電流の表示
 *
 */

 #define abs(x)	((x)>=0?(x):-(x))
 
//電圧を表示する
//引数：	mV単位の値
void	lcdPutVoltage(int16_t mValue, int xPos, int yPos)
{
	uint16_t num, dec;

	lcdLineClear(xPos, yPos);
	lcdSetPos(xPos * 8, yPos);	
	
	// 符号の表示
	if (mValue < 0)
		lcdPutChar('-');
	else
		lcdPutChar(' ');
		
	num = abs(mValue) / 1000;	//整数部
	dec = abs(mValue) % 1000;	//小数部
	
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
void	lcdPutCurrent(int16_t mValue, int xPos, int yPos)
{
	uint16_t	num, dec;
	
	lcdLineClear(xPos, yPos);
	lcdSetPos(xPos * 8, yPos);
	
	// 符号の表示
	if (mValue < 0)
		lcdPutChar('-');
	else
		lcdPutChar(' ');
	
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

//A/D変換スリープからの復帰
ISR(ADC_vect)
{
	//処理なし
}

//ADMUXに設定する基準電圧と入力ピンの内容
//REFS0 -> 00:AREF, 01:AVCC, 11:内部基準1.1V
//MUX0  -> 0111-0000:PC7-PC0, 1110:内部的に1.1Vを接続, 1111:内部的に0Vを接続
#define	ADC_SET_PVOLTAGE	((0b01 << REFS0) | (0b0000 << MUX0))
#define	ADC_SET_GND			((0b01 << REFS0) | (0b0001 << MUX0))
#define	ADC_SET_NVOLTAGE	((0b01 << REFS0) | (0b0010 << MUX0))
#define	ADC_SET_CURRENT		((0b01 << REFS0) | (0b0011 << MUX0))

int main(void)
{
	uint16_t	adcPVoltages[BUFSIZE] = {0UL};
	uint16_t	adcNVoltages[BUFSIZE] = {0UL};
	uint16_t	adcCurrents[BUFSIZE] = {0UL};
	uint16_t	adcGNDs[BUFSIZE] = {0UL};

	uint16_t	adcValue;
	int16_t	gndValue;
	int16_t	mVoltage, mCurrent;
	uint8_t	idx;
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
	lcdSetPos(0, 0);	lcdPutStr("Volt + / Current");
	lcdSetPos(0, 1);	lcdPutStr("Volt - / GND");
	wait_sec(3);
	
	//消費電力削減のためADC使用ピンをデジタル入力禁止にする（ADC入力（アナログ）専用となる）
	DIDR0 |= (1 << ADC0D) | (1 << ADC1D) | (1 << ADC2D) | (1 << ADC3D);	//PC0,PC1,PC2,PC3
	
	//ADC動作設定
	ADCSRA = 0b10000110		//bit2-0: 110 = 64分周	8MHz/64=125kHz (50k～200kHzであること)
		| (1 << ADIE);		//割り込み許可（スリープ対応）
	
	//ADCの基準電圧(AVCC)と入力ピンを設定する
	ADMUX =	(1 << REFS0) | ADC_SET_PVOLTAGE;

	
	//A/D変換ノイズ低減のスリープモードを設定する
	set_sleep_mode(SLEEP_MODE_ADC);
	sei();			//割り込み許可
	sleep_mode();	//A/Dコンバータ初期化として初回変換開始…変換中…変換完了
	adcValue = ADC;	//値の読み捨て

	idx = 0;
	
    while(1)
    {
		//===== GND電圧を測定する ===================================================
		//
		ADMUX =	ADC_SET_GND;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
		
		adcGNDs[idx] = ADC;
		adcValue = total(adcGNDs);
		gndValue = (float)adcValue / BUFSIZE;
		fv = gndValue * F_VCC * 1000 / 1024;	// mV単位
		
		//電圧を表示する
		mVoltage = (int16_t)fv;
		lcdPutVoltage(mVoltage, 1, 1);
		
        //===== 正電圧を測定する ===================================================
		//
		ADMUX =	ADC_SET_PVOLTAGE;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
		
		adcPVoltages[idx] = ADC;
		adcValue = total(adcPVoltages);
			
		//A/D変換値から電圧を求める
		fv = (((float)adcValue / BUFSIZE) - gndValue) * F_VCC * 1000 / (1024 * F_POSITIVE_V_RATE); // mV単位
			
		//電圧を表示する
		mVoltage = (int16_t)fv;
		lcdPutVoltage(mVoltage, 0, 0);
					
		//===== 負電圧を測定する ===================================================
		//
		ADMUX =	ADC_SET_NVOLTAGE;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
			
		adcNVoltages[idx] = ADC;
		adcValue = total(adcNVoltages);
			
		//A/D変換値から電圧を求める
		fv = (((float)adcValue / BUFSIZE) - gndValue) * F_VCC * 1000 / (1024 * F_NEGATIVE_V_RATE); // mV単位
					
		//電圧を表示する
		mVoltage = (int16_t)fv;
		lcdPutVoltage(mVoltage, 0, 1);
			
		//===== 電流を測定する ====================================================
		//
		ADMUX =	ADC_SET_CURRENT;
		wait_ms(5);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
			
		adcCurrents[idx] = ADC;
		adcValue = total(adcCurrents);

		// ADCの読み取り値が1000以上の場合測定不可とする
		if ((float)adcValue / BUFSIZE < 1000) {
	
			//A/D変換値から電圧を求め、電流を算出する
			fv = (((float)adcValue / BUFSIZE) - gndValue) * F_VCC * 1000 / (1024 * F_CURRENT_AMP); // mV単位
			fa = fv / F_SHUNT_R;	//mA単位
				
			//電流を表示する
			mCurrent = (int16_t)(fa * 10.0);	//0.1mA単位
			lcdPutCurrent(mCurrent, 1, 0);
		}
		else {		
			lcdLineClear(1, 0);
			lcdSetPos(8, 0);
			lcdPutStr("OVER");
		}
		
		//次のループの準備
		if (++idx == BUFSIZE) idx = 0;
    }
	
	return 0;
}
