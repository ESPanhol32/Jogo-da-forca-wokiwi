#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

#include "esp_rom_sys.h"
#include "esp_random.h"

#define BTN_NEXT       25
#define BTN_PREV       26
#define BTN_CONFIRM    27

#define LCD_SDA        16
#define LCD_SCL        17

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_FREQ       100000

#define LCD_ADDR       0x27

// LED invertido: LED que apaga primeiro é o último da lista
int led_pins[6] = {23,22,21,19,18,5};

QueueHandle_t button_queue;

typedef enum {
    EVENT_NEXT,
    EVENT_PREV,
    EVENT_CONFIRM
} button_event_t;

typedef enum {
    STATE_MENU,
    STATE_SELECT,
    STATE_CHECK,
    STATE_WIN,
    STATE_LOSE
} game_state_t;

// Palavras em português
const char *words[] = {
    "AMOR_RECIPROCO",
    "CACHORRO",
    "GATO",
    "CASA",
    "ESCOLA",
    "JANELA",
    "MACA",
    "COMPUTADOR",
    "TELEFONE",
    "CARRO",
    "MOTO",
    "AVIAO",
    "BICICLETA",
    "BOLA",
    "FUTEBOL",
    "VOLEI",
    "BASQUETE",
    "LIVRO",
    "CANETA",
    "CADERNO",
    "PROFESSOR",
    "ALUNO",
    "BETA",
    "UNIVERSIDADE",
    "HOSPITAL",
    "MERCADO",
    "PADARIA",
    "PRAIA",
    "FLORESTA",
    "RIO",
    "MONTANHA",
    "CHUVA",
    "SOL",
    "NUVEM",
    "VENTO",
    "FOGO",
    "AGUA",
    "TERRA",
    "PLANETA",
    "ESTRELA",
    "GALAXIA",
    "ROBO",
    "CORTISOL",
    "COQUEIRO",
    "PROGRAMADOR",
    "ELETRONICA"
};

#define WORD_COUNT 46

char current_word[20];
char hidden_word[20];

char selected_letter = 'A';
int lives = 6;
game_state_t state;
int last_state = -1; // Para controlar atualização do LCD

/* ========================================================= */
/* LCD */
/* ========================================================= */

void lcd_send_nibble(uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data | 0x08, true);
    i2c_master_write_byte(cmd, data | 0x0C, true);
    i2c_master_write_byte(cmd, data | 0x08, true);
    i2c_master_stop(cmd);

    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
}

void lcd_send_byte(uint8_t data, uint8_t mode)
{
    uint8_t high = (data & 0xF0) | mode;
    uint8_t low  = ((data << 4) & 0xF0) | mode;
    lcd_send_nibble(high);
    lcd_send_nibble(low);
}

void lcd_command(uint8_t cmd) { lcd_send_byte(cmd, 0); }
void lcd_data(uint8_t data) { lcd_send_byte(data, 1); }
void lcd_clear() { lcd_command(0x01); vTaskDelay(pdMS_TO_TICKS(5)); }
void lcd_set_cursor(int col, int row) { lcd_command((row==0?0x80:0xC0)+col); }
void lcd_print(char *text) { while(*text) lcd_data(*text++); }

void lcd_init()
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = LCD_SDA,
        .scl_io_num = LCD_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ
    };

    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_send_nibble(0x30);
    lcd_send_nibble(0x30);
    lcd_send_nibble(0x30);
    lcd_send_nibble(0x20);

    lcd_command(0x28); // 4-bit, 2 linhas
    lcd_command(0x0C); // display ligado
    lcd_command(0x06); // incremento cursor
    lcd_clear();
}

/* ========================================================= */
/* LEDs */
/* ========================================================= */

void leds_init()
{
    gpio_config_t io = {};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = 0;
    for(int i = 0; i < 6; i++) io.pin_bit_mask |= (1ULL << led_pins[i]);
    gpio_config(&io);
}

void leds_update()
{
    for(int i = 0; i < 6; i++)
    {
        gpio_set_level(led_pins[i], i < lives);
    }
}

/* ========================================================= */
/* BOTÕES */
/* ========================================================= */

void buttons_task(void *pvParameters)
{
    gpio_set_direction(BTN_NEXT, GPIO_MODE_INPUT);
    gpio_set_direction(BTN_PREV, GPIO_MODE_INPUT);
    gpio_set_direction(BTN_CONFIRM, GPIO_MODE_INPUT);

    gpio_pullup_en(BTN_NEXT);
    gpio_pullup_en(BTN_PREV);
    gpio_pullup_en(BTN_CONFIRM);

    while(1)
    {
        if(!gpio_get_level(BTN_NEXT))
        {
            button_event_t ev = EVENT_NEXT;
            xQueueSend(button_queue, &ev, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_PREV))
        {
            button_event_t ev = EVENT_PREV;
            xQueueSend(button_queue, &ev, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_CONFIRM))
        {
            button_event_t ev = EVENT_CONFIRM;
            xQueueSend(button_queue, &ev, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ========================================================= */
/* JOGO */
/* ========================================================= */

void generate_hidden()
{
    int len = strlen(current_word);
    for(int i=0;i<len;i++) hidden_word[i] = '_';
    hidden_word[len] = '\0';
}

int check_win() { return strcmp(current_word, hidden_word) == 0; }

void reset_game()
{
    strcpy(current_word, words[esp_random()%WORD_COUNT]);
    generate_hidden();
    selected_letter = 'A';
    lives = 6;
    leds_update();
}

// Funções para renderizar LCD sem piscar
void lcd_render_menu()
{
    lcd_clear();
    lcd_set_cursor(0,0); 
    lcd_print("JOGO FORCA");
    lcd_set_cursor(0,1); 
    lcd_print("CONFIRMAR");
}

void lcd_render_select()
{
    char line2[16];
    lcd_clear();
    lcd_set_cursor(0,0); 
    lcd_print(hidden_word);
    sprintf(line2,"LETRA:%c",selected_letter);
    lcd_set_cursor(0,1); 
    lcd_print(line2);
}

void lcd_render_win()
{
    lcd_clear();
    lcd_set_cursor(0,0); 
    lcd_print("VOCE VENCEU");
    lcd_set_cursor(0,1); 
    lcd_print(current_word);
}

void lcd_render_lose()
{
    lcd_clear();
    lcd_set_cursor(0,0); 
    lcd_print("VOCE PERDEU");
    lcd_set_cursor(0,1); 
    lcd_print(current_word);
}

void game_task(void *pvParameters)
{
    button_event_t ev;
    reset_game();
    state = STATE_MENU;

    while(1)
    {
        // Atualiza LCD apenas quando mudar de estado
        if(state != last_state)
        {
            last_state = state;
            if(state == STATE_MENU) lcd_render_menu();
            else if(state == STATE_SELECT) lcd_render_select();
            else if(state == STATE_WIN) lcd_render_win();
            else if(state == STATE_LOSE) lcd_render_lose();
        }

        switch(state)
        {
            case STATE_MENU:
                if(xQueueReceive(button_queue,&ev,pdMS_TO_TICKS(100)))
                {
                    if(ev==EVENT_CONFIRM) state = STATE_SELECT;
                }
                break;

            case STATE_SELECT:
                if(xQueueReceive(button_queue,&ev,pdMS_TO_TICKS(100)))
                {
                    if(ev==EVENT_NEXT)
                    { 
                        selected_letter++; 
                        if(selected_letter>'Z') selected_letter='A'; 
                        lcd_render_select(); 
                    }
                    if(ev==EVENT_PREV)
                    { 
                        selected_letter--; 
                        if(selected_letter<'A') selected_letter='Z'; 
                        lcd_render_select(); 
                    }
                    if(ev==EVENT_CONFIRM)
                    { 
                        state=STATE_CHECK; 
                    }
                }
                break;

            case STATE_CHECK:
            {
                int hit=0;
                for(int i=0;i<strlen(current_word);i++)
                {
                    if(current_word[i]==selected_letter)
                    {
                        hidden_word[i]=selected_letter;
                        hit=1;
                    }
                }
                if(!hit){ lives--; leds_update(); }

                if(check_win()) state=STATE_WIN;
                else if(lives<=0) state=STATE_LOSE;
                else state=STATE_SELECT;
            }
            break;

            case STATE_WIN:
                vTaskDelay(pdMS_TO_TICKS(3000));
                reset_game();
                last_state = -1;
                state = STATE_MENU;
                break;

            case STATE_LOSE:
                vTaskDelay(pdMS_TO_TICKS(3000));
                reset_game();
                last_state = -1;
                state = STATE_MENU;
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ========================================================= */
/* MAIN */
/* ========================================================= */

void app_main()
{
    lcd_init();
    leds_init();
    leds_update();

    button_queue = xQueueCreate(10, sizeof(button_event_t));

    xTaskCreate(buttons_task, "buttons_task", 2048, NULL, 5, NULL);
    xTaskCreate(game_task, "game_task", 4096, NULL, 5, NULL);
}
