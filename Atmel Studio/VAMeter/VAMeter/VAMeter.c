/*
 * VAMeter.c
 *
 * Created: 2014/09/07 11:03:43
 *  Author: gizmo
 */ 


//	ATmega88
//	LM317使用 可変電圧源（簡易電圧計・簡易電流計付き）

/*
ポート対応	[ATmega88]	x:存在しない(PC6はRESET) -:空き
bit		7	6	5	4	3	2	1	0
portB	-	-	-	-	-	-	-	CKO		PB0:システムクロック出力
portC	x	x	-	-	-	-	VM	AM		ADC(PC1:電圧計, PC0:電流計)
portD	-	E	RS	-	DB7	DB6	DB5	DB4		LCD接続

【ヒューズビット】(avrsp.exe -rf)	※システムクロックを出力する以外は工場出荷設定
Low: 22 High: DF Ext: 07

Low: 00100010
     ||||++++-- CKSEL[3:0] システムクロック選択
     ||++-- SUT[1:0] 起動時間
     |+-- CKOUT (0:PB0にシステムクロックを出力)
     +-- CKDIV8 クロック分周初期値 (1:1/1, 0:1/8)

High:11-11111
     |||||+++-- BODLEVEL[2:0] (111:無, 110:1.8V, 101:2.7V, 100:4.3V)
     ||||+-- EESAVE (消去でEEPROMを 1:消去, 0:保持)
     |||+-- WDTON (1:WDT通常動作, 0:WDT常時ON)
     ||+-- SPIEN (1:ISP禁止, 0:ISP許可) ※Parallel時のみ
     |+-- DWEN (On-Chipデバッグ 1:無効, 0:有効)
     +-- RSTDISBL (RESETピン 1:有効, 0:無効(PC6))

Ext: -----001
          ||+-- BOOTRST ※データシート参照
          ++-- BOOTSZ[1:0] ※データシート参照

ビルド環境
WinAVR-20080610		最適化オプション:-Os	数学ライブラリを追加:libm.a
ATmega88	1MHz（8MHzの8分周）

履歴
2011/04/17	v1.00	Program:    2498 bytes (30.5% Full)
					Data:         37 bytes (3.6% Full)
				・初版
*/


#include	<avr/io.h>
#include	<avr/sleep.h>
#include	<avr/interrupt.h>
#include	"lcdlib.h"		//F_CPU定義と<util/delay.h>を含む

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


//秒単位の時間待ち
void	delay_sec(uint8_t sec)
{
	uint8_t	i, m;
	for (i = 0; i < sec; i++)
		for (m = 0; m < 5; m++)
			_delay_ms(200);
}

//LCDの表示を1行消去する
void	lcdLineClear(uint8_t y)
{
	lcdSetPos(0, y);
	lcdPutStr("                ");	//16桁
}

//電圧を表示する
//引数：	mV単位の値
void	lcdPutVoltage(uint16_t mValue)
{
	uint16_t	num, dec;
	
	lcdLineClear(VOLTAGE_Y);
	lcdSetPos(VOLTAGE_X, VOLTAGE_Y);
	
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
	
	lcdPutStr(" V");
}

//電流を表示する
//引数：	0.1mA単位の値
void	lcdPutCurrent(uint16_t mValue)
{
	uint16_t	num, dec;
	
	lcdLineClear(CURRENT_Y);
	lcdSetPos(CURRENT_X, CURRENT_Y);
	
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
	
	lcdPutStr(" mA");
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
	
	lcdInit();	//LCD初期化
	
	//LCD初期表示
	lcdSetPos(0, VOLTAGE_Y);	lcdPutStr("Voltage");
	lcdSetPos(0, CURRENT_Y);	lcdPutStr("Current");
	delay_sec(3);
	
	//ノイズ低減を期待してADCのポートをプルアップしておく
	DDRC	= 0b00000000;	//portC全部入力方向
	PORTC	= ~((1 << PC0) | (1 << PC1));	//使用するADC入力ピン以外をプルアップ
	
	//消費電力削減のためADC使用ピンをデジタル入力禁止にする（ADC入力（アナログ）専用となる）
	DIDR0 |= (1 << ADC0D) | (1 << ADC1D);	//PC0,PC1
	
	//ADC動作設定
	ADCSRA = 0b10000100		//bit2-0: 100 = 16分周	1MHz/16=62.5kHz (50k～200kHzであること)
		| (1 << ADIE);		//割り込み許可（スリープ対応）
	
	//ADCの基準電圧と入力ピンを設定する
	ADMUX =	ADC_SET_VOLTAGE;
	
	//A/D変換ノイズ低減のスリープモードを設定する
	set_sleep_mode(SLEEP_MODE_ADC);
	sei();			//割り込み許可
	sleep_mode();	//A/Dコンバータ初期化として初回変換開始…変換中…変換完了
	adcValue = ADC;	//値の読み捨て
	
	idx = 0;
	while(1)
	{
		//===== 電圧を測定する =====
		ADMUX =	ADC_SET_VOLTAGE;
		_delay_ms(10);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
		
		adcVoltages[idx] = ADC;
		adcValue = total(adcVoltages);
		
		//A/D変換値から電圧を求める
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV単位
		fv *= (F_VMETER_RA + F_VMETER_RB) / F_VMETER_RA;	//分圧している値から本来の電圧を求める
															//ここでfv=F_VREFとした値が測定可能な上限値である
		//補正
		//テスターでの実測値と比較して、計算値はF_VREFを中心に約1％ずれていたので補正する。
		//・計算値がF_VREFより大きいとき、その値は実測値より約1％大きかった。
		//・計算値がF_VREFより小さいとき、その値は実測値より約1％小さかった。
		fv -= (fv - F_VREF * 1000) * 0.01;	//fvはmV単位であることに注意
		
		//電圧を表示する
		mVoltage = (uint16_t)fv;
		lcdPutVoltage(mVoltage);
		
		//===== 電流を測定する =====
		ADMUX =	ADC_SET_CURRENT;
		_delay_ms(10);	//安定待ち
		sleep_mode();	//スリープモード突入…変換中…変換完了
		
		adcCurrents[idx] = ADC;
		adcValue = total(adcCurrents);
		
		//A/D変換値から電圧を求め、電流を算出する
		fv = ((float)adcValue / BUFSIZE) * (F_VREF * 1000) / 1024;	//mV単位
		fa = fv / F_SHUNT_R;	//mA単位
		
		//電流を表示する
		mCurrent = (uint16_t)(fa * 10.0);	//0.1mA単位
		lcdPutCurrent(mCurrent);
		
		//次のループの準備
		if (++idx == BUFSIZE) idx = 0;
	}
	
	return 0;
}