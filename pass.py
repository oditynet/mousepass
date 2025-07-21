import pygame
import sys
import time
import os
import json
import numpy as np
from pygame.locals import *

# Инициализация Pygame
pygame.init()

# Константы
SCREEN_WIDTH, SCREEN_HEIGHT = 800, 600
BG_COLOR = (240, 240, 240)
RECORD_COLOR = (255, 200, 200)
VERIFY_COLOR = (200, 255, 200)
TEXT_COLOR = (50, 50, 50)
PATH_COLOR = (70, 130, 180)
CLICK_COLOR = (220, 60, 60)
FONT_SIZE = 24
PASSWORD_FILE = "mouse_password.json"
RECORD_TIME = 4  # время записи пароля в секундах

# Настройка окна
screen = pygame.display.set_mode((SCREEN_WIDTH, SCREEN_HEIGHT))
pygame.display.set_caption("Mouse Password Authentication")
font = pygame.font.SysFont(None, FONT_SIZE)
clock = pygame.time.Clock()

class MousePassword:
    def __init__(self):
        self.reset()
        self.reference = None
        self.load_password()
    
    def reset(self):
        self.events = []
        self.recording = False
        self.verifying = False
        self.start_time = 0
        self.message = ""
        self.message_color = TEXT_COLOR
    
    def start_recording(self):
        self.reset()
        self.recording = True
        self.start_time = time.time()
        self.message = "Записывайте пароль в течение 7 секунд"
    
    def start_verification(self):
        if not self.reference:
            self.message = "Сначала запишите пароль!"
            return
            
        self.reset()
        self.verifying = True
        self.start_time = time.time()
        self.message = "Повторите пароль в течение 7 секунд"
    
    def record_event(self, event_type, pos):
        if not (self.recording or self.verifying):
            return
            
        current_time = time.time() - self.start_time
        if current_time >= RECORD_TIME:
            self.finish_input()
            return
            
        # Фильтрация микро-движений
        if event_type == "move" and self.events:
            last_pos = self.events[-1]["pos"]
            distance = ((pos[0] - last_pos[0])**2 + (pos[1] - last_pos[1])**2)**0.5
            if distance < 2:  # игнорировать микро-движения
                return
        
        self.events.append({
            "type": event_type,
            "pos": pos,
            "time": current_time
        })
    
    def finish_input(self):
        if not self.events:
            self.message = "Пароль не введен!"
            self.recording = False
            self.verifying = False
            return
            
        normalized = self.normalize_events(self.events)
        
        if self.recording:
            self.reference = normalized
            self.save_password()
            self.message = "Пароль успешно сохранен!"
            self.recording = False
        elif self.verifying:
            similarity = self.compare_events(self.reference, normalized)
            threshold = 0.85  # порог совпадения
            
            if similarity >= threshold:
                self.message = f"✅ Пароль верный! Сходство: {similarity:.2f}"
                self.message_color = (0, 150, 0)
            else:
                self.message = f"❌ Пароль неверный! Сходство: {similarity:.2f}"
                self.message_color = (180, 0, 0)
            
            self.verifying = False
    
    def normalize_events(self, events):
        """Нормализация событий для сравнения"""
        if not events:
            return []
        
        # Вычисление границ
        x_coords = [e["pos"][0] for e in events]
        y_coords = [e["pos"][1] for e in events]
        min_x, max_x = min(x_coords), max(x_coords)
        min_y, max_y = min(y_coords), max(y_coords)
        
        # Масштаб для нормализации
        width = max(1, max_x - min_x)
        height = max(1, max_y - min_y)
        scale = max(width, height)
        
        # Нормализация координат и времени
        normalized = []
        for event in events:
            norm_x = (event["pos"][0] - min_x) / scale
            norm_y = (event["pos"][1] - min_y) / scale
            norm_time = event["time"] / RECORD_TIME
            
            normalized.append({
                "type": event["type"],
                "pos": (norm_x, norm_y),
                "time": norm_time
            })
        
        return normalized
    
    def compare_events(self, ref_events, input_events):
        """Сравнение двух последовательностей событий"""
        # Весовые коэффициенты для разных аспектов
        POSITION_WEIGHT = 0.6
        TIME_WEIGHT = 0.3
        SEQUENCE_WEIGHT = 0.1
        
        # Проверка количества кликов
        ref_clicks = [e for e in ref_events if e["type"] == "click"]
        input_clicks = [e for e in input_events if e["type"] == "click"]
        
        if len(ref_clicks) != len(input_clicks):
            return 0.0  # разное количество кликов
        
        # Сравнение позиций и времени
        position_score = 0
        time_score = 0
        sequence_score = 0
        
        min_length = min(len(ref_events), len(input_events))
        
        for i in range(min_length):
            # Сравнение позиций
            ref_pos = ref_events[i]["pos"]
            in_pos = input_events[i]["pos"]
            dx = ref_pos[0] - in_pos[0]
            dy = ref_pos[1] - in_pos[1]
            position_score += 1 - min(1, (dx**2 + dy**2)**0.5)
            
            # Сравнение времени
            time_diff = abs(ref_events[i]["time"] - input_events[i]["time"])
            time_score += 1 - min(1, time_diff)
        
        # Нормализация оценок
        position_score /= min_length
        time_score /= min_length
        
        # Сравнение последовательности событий
        sequence_score = sum(
            1 for i in range(min_length) 
            if ref_events[i]["type"] == input_events[i]["type"]
        ) / min_length
        
        # Итоговое сходство
        similarity = (
            position_score * POSITION_WEIGHT +
            time_score * TIME_WEIGHT +
            sequence_score * SEQUENCE_WEIGHT
        )
        
        return similarity
    
    def save_password(self):
        """Сохранение пароля в файл"""
        if not self.reference:
            return
            
        try:
            with open(PASSWORD_FILE, 'w') as f:
                json.dump(self.reference, f)
        except Exception as e:
            print(f"Ошибка сохранения пароля: {e}")
    
    def load_password(self):
        """Загрузка пароля из файла"""
        if not os.path.exists(PASSWORD_FILE):
            return
            
        try:
            with open(PASSWORD_FILE, 'r') as f:
                self.reference = json.load(f)
        except Exception as e:
            print(f"Ошибка загрузки пароля: {e}")

# Создаем объект для работы с паролем
password_manager = MousePassword()

# Основной цикл
running = True
last_pos = None

while running:
    current_time = time.time()
    screen.fill(BG_COLOR)
    
    # Обработка событий
    for event in pygame.event.get():
        if event.type == QUIT:
            running = False
        
        elif event.type == KEYDOWN:
            if event.key == K_r:
                password_manager.start_recording()
            elif event.key == K_v:
                password_manager.start_verification()
            elif event.key == K_c:
                password_manager.reset()
                password_manager.message = "Пароль сброшен"
            elif event.key == K_ESCAPE:
                running = False
        
        elif event.type == MOUSEMOTION:
            if password_manager.recording or password_manager.verifying:
                password_manager.record_event("move", event.pos)
                last_pos = event.pos
        
        elif event.type == MOUSEBUTTONDOWN:
            if password_manager.recording or password_manager.verifying:
                password_manager.record_event("click", event.pos)
                last_pos = event.pos
    
    # Проверка таймаута ввода
    if (password_manager.recording or password_manager.verifying):
        elapsed = current_time - password_manager.start_time
        if elapsed >= RECORD_TIME:
            password_manager.finish_input()
    
    # Отрисовка интерфейса
    if password_manager.recording:
        screen.fill(RECORD_COLOR)
    elif password_manager.verifying:
        screen.fill(VERIFY_COLOR)
    
    # Отрисовка пути ввода
    if password_manager.events:
        points = [e["pos"] for e in password_manager.events]
        if len(points) > 1:
            pygame.draw.lines(screen, PATH_COLOR, False, points, 2)
        
        # Отрисовка кликов
        for event in password_manager.events:
            if event["type"] == "click":
                pygame.draw.circle(screen, CLICK_COLOR, event["pos"], 10, 2)
    
    # Отображение времени ввода
    if password_manager.recording or password_manager.verifying:
        elapsed = current_time - password_manager.start_time
        time_left = max(0, RECORD_TIME - elapsed)
        time_text = font.render(f"Осталось: {time_left:.1f} сек", True, TEXT_COLOR)
        screen.blit(time_text, (20, 20))
    
    # Отображение инструкций
    help_text = [
        "Управление:",
        "R - Записать новый пароль",
        "V - Проверить пароль",
        "C - Сбросить",
        "ESC - Выход",
        "",
        "Инструкция:",
        "1. Нажмите R и создайте пароль мышью (7 сек)",
        "2. Нажмите V и повторите пароль для проверки",
        "3. Клики мыши учитываются как часть пароля"
    ]
    
    for i, text in enumerate(help_text):
        text_surface = font.render(text, True, TEXT_COLOR)
        screen.blit(text_surface, (20, 60 + i * 30))
    
    # Отображение статуса
    status_text = font.render(password_manager.message, True, password_manager.message_color)
    screen.blit(status_text, (SCREEN_WIDTH // 2 - status_text.get_width() // 2, SCREEN_HEIGHT - 50))
    
    if password_manager.reference:
        status = font.render("Пароль сохранен в системе", True, (0, 100, 0))
        screen.blit(status, (SCREEN_WIDTH - status.get_width() - 20, 20))
    
    pygame.display.flip()
    clock.tick(60)

pygame.quit()
sys.exit()
