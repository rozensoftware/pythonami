# Simple Snake game for Python68K and AmigaOS.
# Controls: W/A/S/D move, R restarts, ESC/Q quits.

import sys
sys.path.append("lib")

import amiga_gfx
import random


GFX_PLUGIN = "PROGDIR:ext/amiga_gfx/amiga_gfx.py68k"

SCREEN_WIDTH = 320
SCREEN_HEIGHT = 200
CELL_SIZE = 10
BOARD_X = 10
BOARD_Y = 20
BOARD_WIDTH = 30
BOARD_HEIGHT = 17
MOVE_FRAMES = 6


class Snake:
    def __init__(self):
        self.body = [[15, 8], [14, 8], [13, 8]]
        self.dx = 1
        self.dy = 0

    def contains(self, x, y):
        i = 0
        while i < len(self.body):
            if self.body[i][0] == x and self.body[i][1] == y:
                return 1
            i = i + 1
        return 0

    def turn(self, dx, dy):
        if dx + self.dx != 0 or dy + self.dy != 0:
            self.dx = dx
            self.dy = dy

    def move(self, grow):
        new_x = self.body[0][0] + self.dx
        new_y = self.body[0][1] + self.dy

        check_count = len(self.body)
        if grow == 0:
            check_count = check_count - 1

        i = 0
        while i < check_count:
            if self.body[i][0] == new_x and self.body[i][1] == new_y:
                return 0
            i = i + 1

        if grow:
            tail = self.body[len(self.body) - 1]
            self.body.append([tail[0], tail[1]])

        i = len(self.body) - 1
        while i > 0:
            self.body[i][0] = self.body[i - 1][0]
            self.body[i][1] = self.body[i - 1][1]
            i = i - 1

        self.body[0][0] = new_x
        self.body[0][1] = new_y
        return 1


class SnakeGame:
    def __init__(self, gfx):
        self.gfx = gfx
        self.snake = Snake()
        self.food_x = 0
        self.food_y = 0
        self.score = 0
        self.alive = 1
        self.won = 0
        self.place_food()

    def reset(self):
        self.snake = Snake()
        self.score = 0
        self.alive = 1
        self.won = 0
        self.place_food()

    def place_food(self):
        if len(self.snake.body) >= BOARD_WIDTH * BOARD_HEIGHT:
            self.alive = 0
            self.won = 1
            return None

        placed = 0
        while placed == 0:
            x = random.randint(0, BOARD_WIDTH - 1)
            y = random.randint(0, BOARD_HEIGHT - 1)
            if self.snake.contains(x, y) == 0:
                self.food_x = x
                self.food_y = y
                placed = 1
        return None

    def handle_key(self, key):
        if self.alive:
            if key == 119 or key == 87:
                self.snake.turn(0, -1)
            elif key == 115 or key == 83:
                self.snake.turn(0, 1)
            elif key == 97 or key == 65:
                self.snake.turn(-1, 0)
            elif key == 100 or key == 68:
                self.snake.turn(1, 0)
        elif key == 114 or key == 82:
            self.reset()

    def update(self):
        head_x = self.snake.body[0][0] + self.snake.dx
        head_y = self.snake.body[0][1] + self.snake.dy

        if head_x < 0 or head_x >= BOARD_WIDTH:
            self.alive = 0
            return None
        if head_y < 0 or head_y >= BOARD_HEIGHT:
            self.alive = 0
            return None

        grow = 0
        if head_x == self.food_x and head_y == self.food_y:
            grow = 1

        if self.snake.move(grow) == 0:
            self.alive = 0
            return None

        if grow:
            self.score = self.score + 1
            self.place_food()
        return None

    def draw_cell(self, x, y, pen):
        self.gfx.ink(pen)
        self.gfx.rect(BOARD_X + x * CELL_SIZE + 1, BOARD_Y + y * CELL_SIZE + 1, CELL_SIZE - 1, CELL_SIZE - 1, 1)

    def draw(self):
        self.gfx.ink(0)
        self.gfx.rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 1)

        self.gfx.ink(1)
        self.gfx.text(10, 10, "SNAKE  SCORE " + str(self.score))
        self.gfx.rect(BOARD_X - 1, BOARD_Y - 1, BOARD_WIDTH * CELL_SIZE + 2, BOARD_HEIGHT * CELL_SIZE + 2, 0)

        self.draw_cell(self.food_x, self.food_y, 2)

        i = len(self.snake.body) - 1
        while i > 0:
            self.draw_cell(self.snake.body[i][0], self.snake.body[i][1], 4)
            i = i - 1
        self.draw_cell(self.snake.body[0][0], self.snake.body[0][1], 3)

        if self.alive == 0:
            self.gfx.ink(1)
            if self.won:
                self.gfx.text(112, 96, "YOU WIN")
            else:
                self.gfx.text(112, 90, "GAME OVER")
                self.gfx.text(96, 104, "PRESS R TO RESTART")


random.seed(time_tick())
gfx = load_library(GFX_PLUGIN)
if gfx.init() != 0:
    print("amiga_gfx init failed")
    exit(1)

screen = amiga_gfx.open_lores_8(gfx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT)
if screen == 0:
    print("open_screen failed")
    gfx.shutdown()
    exit(1)

gfx.color(0, 0, 0, 1)
gfx.color(1, 15, 15, 15)
gfx.color(2, 15, 3, 2)
gfx.color(3, 15, 15, 2)
gfx.color(4, 2, 13, 5)

game = SnakeGame(gfx)
game.draw()

frame = 0
running = 1
while running:
    if gfx.poll() == amiga_gfx.EVT_CLOSE:
        running = 0
    else:
        key = gfx.key()
        game.handle_key(key)

        if game.alive:
            frame = frame + 1
            if frame >= MOVE_FRAMES:
                frame = 0
                game.update()
                game.draw()
        elif key == 114 or key == 82:
            frame = 0
            game.draw()

        gfx.wait_tof()

gfx.close_screen()
gfx.shutdown()