/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct
{
  int16_t x;
  int16_t y;
  uint16_t speed_q8;
  uint16_t step_accumulator;
} EnemyState;

typedef struct
{
  int16_t ship_x;
  int16_t bullet_x;
  int16_t bullet_y;
  uint8_t bullet_active;
  uint8_t fire_button_prev;
  uint8_t restart_button_prev;
  uint8_t game_over;
  uint8_t hit_flash_frames;
  uint16_t joy_x;
  uint16_t joy_center;
  uint16_t score;
  uint32_t rng_state;
  EnemyState enemies[2];
} GameState;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define OLED_I2C_ADDRESS        (0x3CU << 1)
#define OLED_WIDTH              128U
#define OLED_HEIGHT             64U
#define OLED_PAGE_COUNT         (OLED_HEIGHT / 8U)
#define OLED_BUFFER_SIZE        (OLED_WIDTH * OLED_PAGE_COUNT)

#define SHIP_WIDTH              9
#define SHIP_HEIGHT             8
#define SHIP_Y                  ((int16_t)(OLED_HEIGHT - SHIP_HEIGHT))

#define BULLET_WIDTH            1
#define BULLET_HEIGHT           3
#define BULLET_SPEED            3

#define ENEMY_COUNT             2
#define ENEMY_WIDTH             7
#define ENEMY_HEIGHT            7
#define ENEMY_BASE_SPEED_Q8     10U
#define ENEMY_SCORE_SPEED_Q8    3U
#define ENEMY_SLOT_SPEED_Q8     10U
#define ENEMY_VARIATION_Q8      20U
#define ENEMY_MAX_SPEED_Q8      180U

#define JOY_DEADZONE            450
#define JOY_SPEED_STEP          350
#define PLAYER_MIN_SPEED        1
#define PLAYER_MAX_SPEED        5
#define FRAME_TIME_MS           25U
#define SCORE_MAX               999U
#define HIT_FLASH_FRAMES        3U

#define FIRE_BUTTON_PIN         GPIO_PIN_5
#define RESTART_BUTTON_PIN      GPIO_PIN_5
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */
static GameState game;
static uint8_t oled_frame[OLED_BUFFER_SIZE];
static uint8_t oled_previous_frame[OLED_BUFFER_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */
static HAL_StatusTypeDef OLED_WriteCommand(uint8_t cmd);
static HAL_StatusTypeDef OLED_WriteData(const uint8_t *buf, uint16_t size);
static void OLED_Init(void);
static void OLED_ClearBuffer(uint8_t *buffer);
static void OLED_FlushBuffer(const uint8_t *current, uint8_t *previous);
static void OLED_SetPixel(uint8_t *buffer, int16_t x, int16_t y);
static void OLED_DrawHLine(uint8_t *buffer, int16_t x, int16_t y, int16_t width);
static void OLED_DrawVLine(uint8_t *buffer, int16_t x, int16_t y, int16_t height);
static void OLED_DrawRect(uint8_t *buffer, int16_t x, int16_t y, int16_t width, int16_t height);
static void OLED_DrawSpriteRows(uint8_t *buffer, int16_t x, int16_t y,
                                const uint16_t *rows, uint8_t width, uint8_t height);
static void OLED_DrawChar5x7(uint8_t *buffer, int16_t x, int16_t y, char c);
static void OLED_DrawText5x7(uint8_t *buffer, int16_t x, int16_t y, const char *text);
static void OLED_DrawNumber3(uint8_t *buffer, int16_t x, int16_t y, uint16_t value);
static uint8_t ButtonPressed(uint16_t pin);
static uint16_t Read_Joystick_X(void);
static uint16_t Calibrate_Joystick_X(void);
static uint32_t NextRandom(GameState *state);
static void SpawnEnemy(GameState *state, EnemyState *enemy, uint8_t slot, uint8_t initial_spawn);
static void Reset_Game(GameState *state);
static int16_t GetShipStepFromJoystick(int32_t joy_delta);
static uint8_t CheckRectCollision(int16_t ax, int16_t ay, uint8_t aw, uint8_t ah,
                                  int16_t bx, int16_t by, uint8_t bw, uint8_t bh);
static void Game_ProcessInput(GameState *state);
static void Game_Update(GameState *state);
static void Game_Render(const GameState *state);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static const uint16_t ship_sprite[SHIP_HEIGHT] = {
  0x010,
  0x038,
  0x07C,
  0x0FE,
  0x1FF,
  0x06C,
  0x0EE,
  0x183
};

static const uint16_t enemy_sprite[ENEMY_HEIGHT] = {
  0x01C,
  0x03E,
  0x06B,
  0x07F,
  0x05D,
  0x063,
  0x022
};

static HAL_StatusTypeDef OLED_WriteCommand(uint8_t cmd)
{
  uint8_t data[2];

  data[0] = 0x00;
  data[1] = cmd;

  return HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDRESS, data, 2, 100);
}

static HAL_StatusTypeDef OLED_WriteData(const uint8_t *buf, uint16_t size)
{
  uint8_t data[129];

  if (size > 128U)
  {
    return HAL_ERROR;
  }

  data[0] = 0x40;
  memcpy(&data[1], buf, size);

  return HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDRESS, data, size + 1U, 100);
}

static void OLED_Init(void)
{
  HAL_Delay(100);

  OLED_WriteCommand(0xAE);
  OLED_WriteCommand(0x20);
  OLED_WriteCommand(0x02);

  OLED_WriteCommand(0xB0);
  OLED_WriteCommand(0xC8);
  OLED_WriteCommand(0x00);
  OLED_WriteCommand(0x10);
  OLED_WriteCommand(0x40);

  OLED_WriteCommand(0x81);
  OLED_WriteCommand(0x7F);

  OLED_WriteCommand(0xA1);
  OLED_WriteCommand(0xA6);
  OLED_WriteCommand(0xA8);
  OLED_WriteCommand(0x3F);

  OLED_WriteCommand(0xA4);
  OLED_WriteCommand(0xD3);
  OLED_WriteCommand(0x00);

  OLED_WriteCommand(0xD5);
  OLED_WriteCommand(0x80);

  OLED_WriteCommand(0xD9);
  OLED_WriteCommand(0xF1);

  OLED_WriteCommand(0xDA);
  OLED_WriteCommand(0x12);

  OLED_WriteCommand(0xDB);
  OLED_WriteCommand(0x40);

  OLED_WriteCommand(0x8D);
  OLED_WriteCommand(0x14);

  OLED_WriteCommand(0xAF);
}

static void OLED_ClearBuffer(uint8_t *buffer)
{
  memset(buffer, 0, OLED_BUFFER_SIZE);
}

static void OLED_FlushBuffer(const uint8_t *current, uint8_t *previous)
{
  uint8_t page;

  for (page = 0; page < OLED_PAGE_COUNT; page++)
  {
    uint16_t base = (uint16_t)page * OLED_WIDTH;
    int16_t start = -1;
    int16_t end = -1;
    uint16_t x;

    for (x = 0; x < OLED_WIDTH; x++)
    {
      if (current[base + x] != previous[base + x])
      {
        if (start < 0)
        {
          start = (int16_t)x;
        }
        end = (int16_t)x;
      }
    }

    if (start < 0)
    {
      continue;
    }

    if (OLED_WriteCommand((uint8_t)(0xB0U + page)) != HAL_OK)
    {
      continue;
    }

    if (OLED_WriteCommand((uint8_t)(start & 0x0F)) != HAL_OK)
    {
      continue;
    }

    if (OLED_WriteCommand((uint8_t)(0x10U | ((uint16_t)start >> 4))) != HAL_OK)
    {
      continue;
    }

    if (OLED_WriteData(&current[base + (uint16_t)start], (uint16_t)(end - start + 1)) != HAL_OK)
    {
      continue;
    }

    memcpy(&previous[base + (uint16_t)start],
           &current[base + (uint16_t)start],
           (size_t)(end - start + 1));
  }
}

static void OLED_SetPixel(uint8_t *buffer, int16_t x, int16_t y)
{
  uint16_t index;

  if ((x < 0) || (x >= (int16_t)OLED_WIDTH) || (y < 0) || (y >= (int16_t)OLED_HEIGHT))
  {
    return;
  }

  index = (uint16_t)x + ((uint16_t)(y / 8) * OLED_WIDTH);
  buffer[index] |= (uint8_t)(1U << (uint8_t)(y % 8));
}

static void OLED_DrawHLine(uint8_t *buffer, int16_t x, int16_t y, int16_t width)
{
  int16_t column;

  for (column = 0; column < width; column++)
  {
    OLED_SetPixel(buffer, x + column, y);
  }
}

static void OLED_DrawVLine(uint8_t *buffer, int16_t x, int16_t y, int16_t height)
{
  int16_t row;

  for (row = 0; row < height; row++)
  {
    OLED_SetPixel(buffer, x, y + row);
  }
}

static void OLED_DrawRect(uint8_t *buffer, int16_t x, int16_t y, int16_t width, int16_t height)
{
  if ((width <= 0) || (height <= 0))
  {
    return;
  }

  OLED_DrawHLine(buffer, x, y, width);
  OLED_DrawHLine(buffer, x, (int16_t)(y + height - 1), width);
  OLED_DrawVLine(buffer, x, y, height);
  OLED_DrawVLine(buffer, (int16_t)(x + width - 1), y, height);
}

static void OLED_DrawSpriteRows(uint8_t *buffer, int16_t x, int16_t y,
                                const uint16_t *rows, uint8_t width, uint8_t height)
{
  uint8_t row;
  uint8_t column;

  for (row = 0; row < height; row++)
  {
    for (column = 0; column < width; column++)
    {
      if ((rows[row] & (uint16_t)(1U << (width - 1U - column))) != 0U)
      {
        OLED_SetPixel(buffer, (int16_t)(x + column), (int16_t)(y + row));
      }
    }
  }
}

static const uint8_t *Glyph5x7(char c)
{
  static const uint8_t glyph_space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t glyph_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
  static const uint8_t glyph_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
  static const uint8_t glyph_2[5] = {0x62, 0x51, 0x49, 0x49, 0x46};
  static const uint8_t glyph_3[5] = {0x22, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t glyph_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
  static const uint8_t glyph_5[5] = {0x2F, 0x49, 0x49, 0x49, 0x31};
  static const uint8_t glyph_6[5] = {0x3E, 0x49, 0x49, 0x49, 0x32};
  static const uint8_t glyph_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
  static const uint8_t glyph_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t glyph_9[5] = {0x26, 0x49, 0x49, 0x49, 0x3E};
  static const uint8_t glyph_A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
  static const uint8_t glyph_C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
  static const uint8_t glyph_E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
  static const uint8_t glyph_G[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
  static const uint8_t glyph_M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
  static const uint8_t glyph_O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
  static const uint8_t glyph_P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
  static const uint8_t glyph_R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
  static const uint8_t glyph_S[5] = {0x26, 0x49, 0x49, 0x49, 0x32};
  static const uint8_t glyph_V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};

  switch (c)
  {
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'A': return glyph_A;
    case 'C': return glyph_C;
    case 'E': return glyph_E;
    case 'G': return glyph_G;
    case 'M': return glyph_M;
    case 'O': return glyph_O;
    case 'P': return glyph_P;
    case 'R': return glyph_R;
    case 'S': return glyph_S;
    case 'V': return glyph_V;
    case ' ': return glyph_space;
    default:  return glyph_space;
  }
}

static void OLED_DrawChar5x7(uint8_t *buffer, int16_t x, int16_t y, char c)
{
  const uint8_t *glyph = Glyph5x7(c);
  uint8_t column;
  uint8_t row;

  for (column = 0; column < 5U; column++)
  {
    for (row = 0; row < 7U; row++)
    {
      if ((glyph[column] & (uint8_t)(1U << row)) != 0U)
      {
        OLED_SetPixel(buffer, (int16_t)(x + column), (int16_t)(y + row));
      }
    }
  }
}

static void OLED_DrawText5x7(uint8_t *buffer, int16_t x, int16_t y, const char *text)
{
  while (*text != '\0')
  {
    OLED_DrawChar5x7(buffer, x, y, *text);
    x += 6;
    text++;
  }
}

static void OLED_DrawNumber3(uint8_t *buffer, int16_t x, int16_t y, uint16_t value)
{
  char digits[4];

  if (value > SCORE_MAX)
  {
    value = SCORE_MAX;
  }

  digits[0] = (char)('0' + ((value / 100U) % 10U));
  digits[1] = (char)('0' + ((value / 10U) % 10U));
  digits[2] = (char)('0' + (value % 10U));
  digits[3] = '\0';

  OLED_DrawText5x7(buffer, x, y, digits);
}

static uint8_t ButtonPressed(uint16_t pin)
{
  return (HAL_GPIO_ReadPin(GPIOA, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static uint16_t Read_Joystick_X(void)
{
  uint16_t value = 2048U;

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return value;
  }

  if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
  {
    value = (uint16_t)HAL_ADC_GetValue(&hadc1);
  }

  HAL_ADC_Stop(&hadc1);

  return value;
}

static uint16_t Calibrate_Joystick_X(void)
{
  uint32_t sum = 0;
  uint8_t sample;

  for (sample = 0; sample < 8U; sample++)
  {
    sum += Read_Joystick_X();
  }

  return (uint16_t)(sum / 8U);
}

static uint32_t NextRandom(GameState *state)
{
  state->rng_state = (state->rng_state * 1664525UL) + 1013904223UL;
  return state->rng_state;
}

static void SpawnEnemy(GameState *state, EnemyState *enemy, uint8_t slot, uint8_t initial_spawn)
{
  uint16_t random_x;
  uint16_t speed_q8;

  random_x = (uint16_t)(NextRandom(state) % (OLED_WIDTH - ENEMY_WIDTH));
  enemy->x = (int16_t)random_x;
  enemy->step_accumulator = 0;

  if (initial_spawn != 0U)
  {
    enemy->y = (int16_t)(-12 - ((int16_t)slot * 18));
  }
  else
  {
    enemy->y = (int16_t)(-10 - (int16_t)(NextRandom(state) % 20U));
  }

  speed_q8 = (uint16_t)(ENEMY_BASE_SPEED_Q8 +
                        (state->score * ENEMY_SCORE_SPEED_Q8) +
                        ((uint16_t)slot * ENEMY_SLOT_SPEED_Q8) +
                        (NextRandom(state) % ENEMY_VARIATION_Q8));
  if (speed_q8 > ENEMY_MAX_SPEED_Q8)
  {
    speed_q8 = ENEMY_MAX_SPEED_Q8;
  }
  enemy->speed_q8 = speed_q8;
}

static void Reset_Game(GameState *state)
{
  uint8_t enemy_index;

  memset(state, 0, sizeof(*state));

  state->ship_x = (int16_t)((OLED_WIDTH - SHIP_WIDTH) / 2U);
  state->joy_center = Calibrate_Joystick_X();
  state->joy_x = state->joy_center;
  state->rng_state = 0xA341316CUL ^ HAL_GetTick() ^ ((uint32_t)state->joy_center << 12);

  if (state->rng_state == 0U)
  {
    state->rng_state = 0x1U;
  }

  for (enemy_index = 0; enemy_index < ENEMY_COUNT; enemy_index++)
  {
    SpawnEnemy(state, &state->enemies[enemy_index], enemy_index, 1U);
  }

  state->fire_button_prev = ButtonPressed(FIRE_BUTTON_PIN);
  state->restart_button_prev = ButtonPressed(RESTART_BUTTON_PIN);
}

static int16_t GetShipStepFromJoystick(int32_t joy_delta)
{
  int32_t abs_delta;
  int16_t speed;

  if ((joy_delta > -(int32_t)JOY_DEADZONE) && (joy_delta < (int32_t)JOY_DEADZONE))
  {
    return 0;
  }

  abs_delta = (joy_delta < 0) ? -joy_delta : joy_delta;
  speed = (int16_t)(PLAYER_MIN_SPEED + ((abs_delta - JOY_DEADZONE) / JOY_SPEED_STEP));

  if (speed > PLAYER_MAX_SPEED)
  {
    speed = PLAYER_MAX_SPEED;
  }

  return (joy_delta < 0) ? (int16_t)(-speed) : speed;
}

static uint8_t CheckRectCollision(int16_t ax, int16_t ay, uint8_t aw, uint8_t ah,
                                  int16_t bx, int16_t by, uint8_t bw, uint8_t bh)
{
  int16_t a_right = (int16_t)(ax + (int16_t)aw - 1);
  int16_t a_bottom = (int16_t)(ay + (int16_t)ah - 1);
  int16_t b_right = (int16_t)(bx + (int16_t)bw - 1);
  int16_t b_bottom = (int16_t)(by + (int16_t)bh - 1);

  if (a_right < bx) return 0U;
  if (ax > b_right) return 0U;
  if (a_bottom < by) return 0U;
  if (ay > b_bottom) return 0U;

  return 1U;
}

static void Game_ProcessInput(GameState *state)
{
  int32_t joy_delta;
  int16_t ship_step;
  uint8_t fire_pressed = ButtonPressed(FIRE_BUTTON_PIN);
  uint8_t restart_pressed = ButtonPressed(RESTART_BUTTON_PIN);

  state->joy_x = Read_Joystick_X();

  if ((restart_pressed != 0U) && (state->restart_button_prev == 0U) && (state->game_over != 0U))
  {
    Reset_Game(state);
    return;
  }

  if (state->game_over == 0U)
  {
    joy_delta = (int32_t)state->joy_x - (int32_t)state->joy_center;
    ship_step = GetShipStepFromJoystick(joy_delta);
    state->ship_x = (int16_t)(state->ship_x + ship_step);

    if (state->ship_x < 0)
    {
      state->ship_x = 0;
    }

    if (state->ship_x > (int16_t)(OLED_WIDTH - SHIP_WIDTH))
    {
      state->ship_x = (int16_t)(OLED_WIDTH - SHIP_WIDTH);
    }

    if ((fire_pressed != 0U) && (state->fire_button_prev == 0U) && (state->bullet_active == 0U))
    {
      state->bullet_active = 1U;
      state->bullet_x = (int16_t)(state->ship_x + (SHIP_WIDTH / 2));
      state->bullet_y = (int16_t)(SHIP_Y - BULLET_HEIGHT);
    }
  }

  state->fire_button_prev = fire_pressed;
  state->restart_button_prev = restart_pressed;
}

static void Game_Update(GameState *state)
{
  uint8_t enemy_index;

  if (state->game_over != 0U)
  {
    return;
  }

  if (state->hit_flash_frames > 0U)
  {
    state->hit_flash_frames--;
  }

  if (state->bullet_active != 0U)
  {
    state->bullet_y -= BULLET_SPEED;

    if ((state->bullet_y + BULLET_HEIGHT) < 0)
    {
      state->bullet_active = 0U;
    }
  }

  for (enemy_index = 0; enemy_index < ENEMY_COUNT; enemy_index++)
  {
    EnemyState *enemy = &state->enemies[enemy_index];

    enemy->step_accumulator = (uint16_t)(enemy->step_accumulator + enemy->speed_q8);
    while (enemy->step_accumulator >= 256U)
    {
      enemy->step_accumulator = (uint16_t)(enemy->step_accumulator - 256U);
      enemy->y++;
    }

    if ((state->bullet_active != 0U) &&
        CheckRectCollision(state->bullet_x, state->bullet_y, BULLET_WIDTH, BULLET_HEIGHT,
                           enemy->x, enemy->y, ENEMY_WIDTH, ENEMY_HEIGHT) != 0U)
    {
      state->bullet_active = 0U;
      if (state->score < SCORE_MAX)
      {
        state->score++;
      }
      state->hit_flash_frames = HIT_FLASH_FRAMES;
      SpawnEnemy(state, enemy, enemy_index, 0U);
      continue;
    }

    if (CheckRectCollision(state->ship_x, SHIP_Y, SHIP_WIDTH, SHIP_HEIGHT,
                           enemy->x, enemy->y, ENEMY_WIDTH, ENEMY_HEIGHT) != 0U)
    {
      state->game_over = 1U;
      break;
    }

    if ((enemy->y + ENEMY_HEIGHT) >= (int16_t)OLED_HEIGHT)
    {
      state->game_over = 1U;
      break;
    }
  }
}

static void Game_Render(const GameState *state)
{
  uint8_t enemy_index;

  OLED_ClearBuffer(oled_frame);

  if (state->hit_flash_frames != 0U)
  {
    OLED_DrawRect(oled_frame, 0, 0, (int16_t)OLED_WIDTH, (int16_t)OLED_HEIGHT);
  }

  if (state->bullet_active != 0U)
  {
    OLED_SetPixel(oled_frame, state->bullet_x, state->bullet_y);
    OLED_SetPixel(oled_frame, state->bullet_x, (int16_t)(state->bullet_y + 1));
    OLED_SetPixel(oled_frame, state->bullet_x, (int16_t)(state->bullet_y + 2));
  }

  for (enemy_index = 0; enemy_index < ENEMY_COUNT; enemy_index++)
  {
    OLED_DrawSpriteRows(oled_frame, state->enemies[enemy_index].x, state->enemies[enemy_index].y,
                        enemy_sprite, ENEMY_WIDTH, ENEMY_HEIGHT);
  }

  OLED_DrawSpriteRows(oled_frame, state->ship_x, SHIP_Y, ship_sprite, SHIP_WIDTH, SHIP_HEIGHT);

  OLED_DrawText5x7(oled_frame, 0, 0, "SCORE");
  OLED_DrawNumber3(oled_frame, 36, 0, state->score);
  OLED_DrawHLine(oled_frame, 0, 8, (int16_t)OLED_WIDTH);

  if (state->game_over != 0U)
  {
    OLED_DrawRect(oled_frame, 18, 18, 92, 32);
    OLED_DrawText5x7(oled_frame, 28, 22, "GAME OVER");
    OLED_DrawText5x7(oled_frame, 24, 34, "SCORE");
    OLED_DrawNumber3(oled_frame, 60, 34, state->score);
  }

  OLED_FlushBuffer(oled_frame, oled_previous_frame);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint32_t next_frame_tick;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();
  OLED_ClearBuffer(oled_frame);
  memset(oled_previous_frame, 0xFF, sizeof(oled_previous_frame));
  Reset_Game(&game);
  Game_Render(&game);
  next_frame_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    if ((int32_t)(now - next_frame_tick) >= 0)
    {
      next_frame_tick += FRAME_TIME_MS;
      Game_ProcessInput(&game);
      Game_Update(&game);
      Game_Render(&game);
    }
    else
    {
      HAL_Delay(1);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
