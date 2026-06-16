#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"

#include "esp_rom_sys.h"
#include "esp_random.h"

/* ========================================================= */
/* GPIOs */
/* ========================================================= */

/* LCD */
#define LCD_RS 14
#define LCD_E  13

#define LCD_D4 16
#define LCD_D5 17
#define LCD_D6 18
#define LCD_D7 19

/* Botões */
#define BTN_NEXT       25
#define BTN_PREV       26
#define BTN_CONFIRM    27

/* LEDs */
static int led_pins[6] = {4,33,32,23,22,21};

/* ========================================================= */
/* Tipos */
/* ========================================================= */

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

/* ========================================================= */
/* Variáveis globais */
/* ========================================================= */

QueueHandle_t button_queue;

char current_word[32];
char hidden_word[32];

char selected_letter = 'A';

int lives = 6;

game_state_t state;
int last_state = -1;

/* ========================================================= */
/* Palavras */
/* ========================================================= */

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

#define WORD_COUNT (sizeof(words)/sizeof(words[0]))

/* ========================================================= */
/* LCD */
/* ========================================================= */

static void lcd_pulse(void)
{
gpio_set_level(LCD_E, 0);
esp_rom_delay_us(1);


gpio_set_level(LCD_E, 1);
esp_rom_delay_us(1);

gpio_set_level(LCD_E, 0);
esp_rom_delay_us(100);


}

static void lcd_write_nibble(uint8_t nibble)
{
gpio_set_level(LCD_D4, (nibble >> 0) & 1);
gpio_set_level(LCD_D5, (nibble >> 1) & 1);
gpio_set_level(LCD_D6, (nibble >> 2) & 1);
gpio_set_level(LCD_D7, (nibble >> 3) & 1);


lcd_pulse();


}

static void lcd_send(uint8_t value, bool rs)
{
    gpio_set_level(LCD_RS, rs);

    lcd_write_nibble(value >> 4);
    lcd_write_nibble(value & 0x0F);
}



static void lcd_command(uint8_t cmd)
{
lcd_send(cmd, false);


if(cmd == 0x01 || cmd == 0x02)
    vTaskDelay(pdMS_TO_TICKS(2));


}

static void lcd_data(uint8_t data)
{
lcd_send(data, true);
}

static void lcd_clear(void)
{
lcd_command(0x01);
}

static void lcd_set_cursor(uint8_t col, uint8_t row)
{
uint8_t addr = (row == 0) ? 0x00 : 0x40;
lcd_command(0x80 | (addr + col));
}

static void lcd_print(const char *text)
{
while(*text)
lcd_data(*text++);
}

static void lcd_init(void)
{
gpio_config_t io = {
.pin_bit_mask =
(1ULL << LCD_RS) |
(1ULL << LCD_E)  |
(1ULL << LCD_D4) |
(1ULL << LCD_D5) |
(1ULL << LCD_D6) |
(1ULL << LCD_D7),
.mode = GPIO_MODE_OUTPUT,
.pull_up_en = 0,
.pull_down_en = 0,
.intr_type = GPIO_INTR_DISABLE
};


gpio_config(&io);

vTaskDelay(pdMS_TO_TICKS(50));

gpio_set_level(LCD_RS, 0);

lcd_write_nibble(0x03);
vTaskDelay(pdMS_TO_TICKS(5));

lcd_write_nibble(0x03);
vTaskDelay(pdMS_TO_TICKS(5));

lcd_write_nibble(0x03);
vTaskDelay(pdMS_TO_TICKS(5));

lcd_write_nibble(0x02);

lcd_command(0x28);
lcd_command(0x0C);
lcd_command(0x06);
lcd_command(0x01);


}

/* ========================================================= */
/* LEDs */
/* ========================================================= */

static void leds_init(void)
{
gpio_config_t io = {
.mode = GPIO_MODE_OUTPUT
};


for(int i=0;i<6;i++)
    io.pin_bit_mask |= (1ULL << led_pins[i]);

gpio_config(&io);


}

static void leds_update(void)
{
for(int i=0;i<6;i++)
gpio_set_level(led_pins[i], i < lives);
}

/* ========================================================= */
/* BOTÕES */
/* ========================================================= */

static void buttons_task(void *arg)
{
gpio_config_t io = {
.pin_bit_mask =
(1ULL << BTN_NEXT) |
(1ULL << BTN_PREV) |
(1ULL << BTN_CONFIRM),
.mode = GPIO_MODE_INPUT,
.pull_up_en = GPIO_PULLUP_ENABLE,
.pull_down_en = GPIO_PULLDOWN_DISABLE,
.intr_type = GPIO_INTR_DISABLE
};


gpio_config(&io);

while(1)
{
    if(!gpio_get_level(BTN_NEXT))
    {
        button_event_t ev = EVENT_NEXT;
        xQueueSend(button_queue,&ev,0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if(!gpio_get_level(BTN_PREV))
    {
        button_event_t ev = EVENT_PREV;
        xQueueSend(button_queue,&ev,0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if(!gpio_get_level(BTN_CONFIRM))
    {
        button_event_t ev = EVENT_CONFIRM;
        xQueueSend(button_queue,&ev,0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    vTaskDelay(pdMS_TO_TICKS(20));
}


}

/* ========================================================= */
/* JOGO */
/* ========================================================= */

static void generate_hidden(void)
{
int len = strlen(current_word);


for(int i=0;i<len;i++)
    hidden_word[i] = '_';

hidden_word[len] = 0;


}

static int check_win(void)
{
return strcmp(current_word, hidden_word) == 0;
}

static void reset_game(void)
{
strcpy(current_word, words[esp_random() % WORD_COUNT]);


generate_hidden();

selected_letter = 'A';
lives = 6;

leds_update();


}

static void lcd_render_menu(void)
{
lcd_clear();


lcd_set_cursor(0,0);
lcd_print("JOGO DA FORCA");

lcd_set_cursor(0,1);
lcd_print("CONFIRMAR");


}

static void lcd_render_select(void)
{
char line1[17];
char line2[17];


snprintf(line1,sizeof(line1),"%.16s",hidden_word);
snprintf(line2,sizeof(line2),"LETRA:%c",selected_letter);

lcd_clear();

lcd_set_cursor(0,0);
lcd_print(line1);

lcd_set_cursor(0,1);
lcd_print(line2);


}

static void lcd_render_win(void)
{
    lcd_clear();

    lcd_set_cursor(0,0);
    lcd_print("VOCE GANHOU");

    lcd_set_cursor(0,1);
    lcd_print(current_word);
}

static void lcd_render_lose(void)
{
lcd_clear();


lcd_set_cursor(0,0);
lcd_print("VOCE PERDEU");

lcd_set_cursor(0,1);
lcd_print(current_word);


}

static void game_task(void *arg)
{
button_event_t ev;


reset_game();

state = STATE_MENU;

while(1)
{
    if(state != last_state)
    {
        last_state = state;

        if(state == STATE_MENU) lcd_render_menu();
        if(state == STATE_SELECT) lcd_render_select();
        if(state == STATE_WIN) lcd_render_win();
        if(state == STATE_LOSE) lcd_render_lose();
    }

    switch(state)
    {
        case STATE_MENU:

            if(xQueueReceive(button_queue,&ev,pdMS_TO_TICKS(100)))
            {
                if(ev == EVENT_CONFIRM)
                {
                    last_state = -1;
                    state = STATE_SELECT;
                }
            }

            break;

        case STATE_SELECT:

            if(xQueueReceive(button_queue,&ev,pdMS_TO_TICKS(100)))
            {
                if(ev == EVENT_NEXT)
                {
                    selected_letter++;

                    if(selected_letter > 'Z')
                        selected_letter = 'A';

                    lcd_render_select();
                }

                if(ev == EVENT_PREV)
                {
                    selected_letter--;

                    if(selected_letter < 'A')
                        selected_letter = 'Z';

                    lcd_render_select();
                }

                if(ev == EVENT_CONFIRM)
                {
                    state = STATE_CHECK;
                }
            }

            break;

        case STATE_CHECK:
        {
            int hit = 0;

            for(int i=0;i<strlen(current_word);i++)
            {
                if(current_word[i] == selected_letter)
                {
                    hidden_word[i] = selected_letter;
                    hit = 1;
                }
            }

            if(!hit)
            {
                lives--;
                leds_update();
            }

            if(check_win())
                state = STATE_WIN;
            else if(lives <= 0)
                state = STATE_LOSE;
            else
            {
                last_state = -1;
                state = STATE_SELECT;
            }

            break;
        }

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

void app_main(void)
{
lcd_init();


leds_init();
leds_update();

button_queue = xQueueCreate(10, sizeof(button_event_t));

xTaskCreate(buttons_task,
            "buttons_task",
            2048,
            NULL,
            5,
            NULL);

xTaskCreate(game_task,
            "game_task",
            4096,
            NULL,
            5,
            NULL);


}
