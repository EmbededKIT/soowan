#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <wiringPi.h>
#include <pthread.h>
#include <softPwm.h>

#define SERVO_PIN 12        // 서보모터 핀
#define SERVO_RANGE 2000    // 서보 모터 PWM 범위

#define DC_START 18         // DC 모터 핀
#define DC_END 19           // DC 모터 핀
#define DC_START_SPEED 100  // 0 ~ 100
#define DC_END_SPEED 3

#define USER_COUNT 3        // 사용자 수
#define CARD_COUNT 2        // 사용자당 카드 수

// 전역 변수 및 조건 변수
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t servo_at_position = PTHREAD_COND_INITIALIZER;

volatile int servo_reached = 0; // 서보 모터가 특정 위치에 도착
volatile float servo_current_angle = 0.0; // 서보 모터의 현재 각도
volatile int card_distributed = 0; // 전체 분배된 카드 수
volatile float user_positions[USER_COUNT] = {-60.0, 0.0, 60.0}; // 사용자 위치

// DC 모터: softPWM, 서보 모터: 기존의 하드웨어 PWM
void init_gpio()
{
    pinMode(SERVO_PIN, PWM_OUTPUT);
    pwmSetMode(PWM_MODE_MS);
    pwmSetRange(SERVO_RANGE);
    pwmSetClock(192);    

    // 초기값 0, 최댓값 100
    softPwmCreate(DC_START, 0, 100);
    softPwmCreate(DC_END, 0, 100);
}

// 서보 모터
void *rotate_servo(void *arg)
{
    int index = 0;
    int direction = 1;

    while (card_distributed < USER_COUNT * CARD_COUNT)
    {
        servo_current_angle = -90.0 + 1.0 * index * direction;

        if (servo_current_angle > 90.0 || servo_current_angle < -90.0)
        {
            direction *= -1;
            index = 0;
            continue;
        }

        int duty = 150 + (servo_current_angle * 100 / 90);
        if (duty < 50) duty = 50;
        if (duty > 250) duty = 250;

        pwmWrite(SERVO_PIN, duty);
        delay(100);

        printf("\r[서보 모터] %.2f도 위치로 이동", servo_current_angle);
        fflush(stdout);

        for (int i = 0; i < USER_COUNT; i++)
        {
            if (fabs(servo_current_angle - user_positions[i]) < 1e-1)
            {
                printf("\n[서보 모터] 사용자 %d의 위치 (%.2f도)에 도달\n", i + 1, user_positions[i]);

                pthread_mutex_lock(&mutex);
                servo_reached = i + 1;
                pthread_cond_signal(&servo_at_position);
                pthread_mutex_unlock(&mutex);
            }
        }

        index++;
    }

    printf("\n[서보 모터] 종료\n");
    return NULL;
}

// DC 모터 동작
void *rotate_dc()
{
    int user_card_count[USER_COUNT] = {0}; // 각 사용자가 받은 카드 수

    while (card_distributed < USER_COUNT * CARD_COUNT)
    {
        pthread_mutex_lock(&mutex);
        while (!servo_reached)
        {
            pthread_cond_wait(&servo_at_position, &mutex);
        }

        int user = servo_reached - 1; // 배열에 저장하기 위해 -1
        servo_reached = 0; // 다시 0으로 초기화하여 wait
        pthread_mutex_unlock(&mutex);

        if (user_card_count[user] < CARD_COUNT)
        {
            user_card_count[user]++;
            card_distributed++;

            // softPWM
            softPwmWrite(DC_START, DC_START_SPEED);
            delay(700);
            softPwmWrite(DC_START, 0);

            softPwmWrite(DC_END, DC_START_SPEED);
            delay(500);
            softPwmWrite(DC_END, 0);

            softPwmWrite(DC_START, DC_END_SPEED);
            delay(100);
            softPwmWrite(DC_START, 0);

            printf("\n[DC 모터] 사용자 %d에게 카드 %d 분배 완료\n", user + 1, user_card_count[user]);
        }
    }

    return NULL;
}

// 메인 함수
int main()
{
    if (wiringPiSetupGpio() == -1)
    {
        printf("GPIO 초기화 실패\n");
        return 1;
    }
    init_gpio();

    pthread_t servo_thread, dc_thread;

    pthread_create(&servo_thread, NULL, rotate_servo, NULL);
    pthread_create(&dc_thread, NULL, rotate_dc, NULL);

    pthread_join(servo_thread, NULL);
    pthread_join(dc_thread, NULL);

    printf("\n[시스템] 모든 카드가 분배되었습니다.\n");

    return 0;
}
