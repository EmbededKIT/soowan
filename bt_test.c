#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <softPwm.h>

#define SERVO_PIN 12     // 서보모터 핀
#define SERVO_RANGE 2000 // 서보 모터 PWM 범위

#define DC_START 18        // DC 모터 핀
#define DC_END 19          // DC 모터 핀
#define DC_START_SPEED 100 // 0 ~ 100
#define DC_END_SPEED 3

#define BAUD_RATE 115200                       // 블루투스 UART 통신 속도
static const char *UART1_DEV = "/dev/ttyAMA1"; // UART 장치 파일

// 전역 변수 및 조건 변수
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t servo_at_position = PTHREAD_COND_INITIALIZER;

volatile int servo_reached = 0;           // 서보 모터가 특정 위치에 도착
volatile float servo_current_angle = 0.0; // 서보 모터의 현재 각도
volatile int card_distributed = 0;        // 전체 분배된 카드 수

volatile int user_count = 0; // 사용자 수
volatile int card_count = 0; // 사용자당 카드 수

float *user_positions = NULL; // 사용자 위치 (동적 할당)

// DC 모터 및 서보 모터 초기화
void init_gpio()
{
    pinMode(SERVO_PIN, PWM_OUTPUT);
    pwmSetMode(PWM_MODE_MS);
    pwmSetRange(SERVO_RANGE);
    pwmSetClock(192);

    softPwmCreate(DC_START, 0, 100);
    softPwmCreate(DC_END, 0, 100);
}

// 블루투스 데이터 수신
unsigned char serialRead(const int fd)
{
    unsigned char x;
    if (read(fd, &x, 1) != 1) // 1바이트 읽기 실패 시 -1 반환
        return -1;
    return x;
}

// 서보 모터 동작
void *rotate_servo(void *arg)
{
    int index = 0;     // 작동 각도
    int direction = 1; // 작동 방향

    while (card_distributed < user_count * card_count)
    {
        // 1도씩 작동
        servo_current_angle = -90.0 + 1.0 * index * direction;

        if (servo_current_angle > 90.0 || servo_current_angle < -90.0)
        {
            direction *= -1;
            index = 0;
            continue;
        }

        int duty = 150 + (servo_current_angle * 100 / 90);
        if (duty < 50)
            duty = 50;
        if (duty > 250)
            duty = 250;

        pwmWrite(SERVO_PIN, duty);
        delay(100);

        printf("\r[서보 모터] %.2f도 위치로 이동\n", servo_current_angle);
        fflush(stdout);

        for (int i = 0; i < user_count; i++)
        {
            // 서보 모터의 현재 각도와 사용자 위치의 차가 0.1보다 작을 때
            if (fabs(servo_current_angle - user_positions[i]) < 1e-1)
            {
                printf("[서보 모터] 사용자 %d의 위치 (%.2f도)에 도달\n", i + 1, user_positions[i]);

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
void *rotate_dc(void *arg)
{
    int *user_card_count = (int *)calloc(user_count, sizeof(int));
    if (user_card_count == NULL)
    {
        printf("메모리 할당 실패\n");
        exit(1);
    }

    while (card_distributed < user_count * card_count)
    {
        pthread_mutex_lock(&mutex);
        while (!servo_reached)
        {
            pthread_cond_wait(&servo_at_position, &mutex);
        }

        int user = servo_reached - 1; // 배열에 저장하기 위해 -1
        servo_reached = 0; // 다시 0으로 초기화하고 wait
        pthread_mutex_unlock(&mutex);

        if (user_card_count[user] < card_count)
        {
            user_card_count[user]++;
            card_distributed++;

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

    free(user_card_count);
    return NULL;
}

// 블루투스 데이터 처리
void process_bluetooth_data(const char *buffer)
{
    if (buffer[0] == 'P' && buffer[2] == 'C')
    {
        user_count = buffer[1] - '0';
        card_count = buffer[3] - '0';

        printf("People number: %d, Card number: %d\n", user_count, card_count);

        if (user_positions != NULL)
        {
            free(user_positions);
        }

        user_positions = (float *)malloc(user_count * sizeof(float));
        if (user_positions == NULL)
        {
            printf("메모리 할당 실패\n");
            exit(1);
        }

        // 사용자 좌표를 코드에서 직접 설정
        float predefined_positions[] = {0.0, 10.0, 20.0, 30.0, 40.0};
        for (int i = 0; i < user_count; i++)
        {
            user_positions[i] = predefined_positions[i]; // 좌표를 할당
            printf("User %d position: %.2f\n", i + 1, user_positions[i]);
        }

        pthread_t servo_thread, dc_thread;
        pthread_create(&servo_thread, NULL, rotate_servo, NULL);
        pthread_create(&dc_thread, NULL, rotate_dc, NULL);

        pthread_join(servo_thread, NULL);
        pthread_join(dc_thread, NULL);

        printf("\n[시스템] 모든 카드가 분배되었습니다.\n");
    }
    else
    {
        printf("무시된 데이터: %s\n", buffer);
    }
}

int main()
{
    int fd_serial;
    unsigned char dat;
    unsigned char buffer[20];
    int buffer_idx = 0;

    if (wiringPiSetupGpio() == -1)
    {
        printf("GPIO 초기화 실패\n");
        return 1;
    }

    init_gpio();

    if ((fd_serial = serialOpen(UART1_DEV, BAUD_RATE)) < 0)
    {
        printf("UART 포트를 열 수 없습니다: %s\n", UART1_DEV);
        return 1;
    }

    while (1)
    {
        if (serialDataAvail(fd_serial))
        {
            dat = serialRead(fd_serial);

            if (dat != '\n' && buffer_idx < sizeof(buffer) - 1)
            {
                buffer[buffer_idx++] = dat;
            }
            else
            {
                buffer[buffer_idx] = '\0';
                buffer_idx = 0;
                process_bluetooth_data(buffer);
            }
        }
        delay(10);
    }

    if (user_positions != NULL)
    {
        free(user_positions);
    }

    return 0;
}
