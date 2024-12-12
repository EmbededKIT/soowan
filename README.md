## card.c
- 임의 지정 값
  - 사용자 수, 사용자당 카드 수
  - 사용자 위치 배열
- DC 모터에 softPWM 사용
- 실행 명령어
  - gcc -o card card.c -lwiringPi -lpthread
 
<br/><br/>

## bt_test.c
- card.c 파일에 블루투스 입력 받기

<br/><br/>

## camera_test.c
- card.c 파일에 카메라 입력 받기

<br/><br/>

## GPIO 핀 번호
### DC 모터
1. VCC: 5V Power(2)
2. GND: GRUOND(39)
3. INA: GPIO 18 (PWM0) (12)
4. INB: GPIO 19 (PWM1) (35)
### 서보 모터
1. VCC (빨간색): 3v3 Power (1)
2. GND (갈색): GROUND (34)
3. CONTROL SIGNAL (주황색): GPIO 12 (PWM0) (32)
### 블루투스 
1. VCC: 5v Power (4)
2. GND: GROUND (6)
3. TXD: GPIO 1 (EEPROM SCL) (28)
4. RXD: GPIO 0 (EEPROM SDA) (27)
