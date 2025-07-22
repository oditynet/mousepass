#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <Imlib2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <float.h>

#define FONT_NAME "DejaVu Sans:size=32:antialias=true"
#define DOT_RADIUS 8
#define DOT_SPACING 20
#define BACKGROUND_COLOR "#000000"
#define TEXT_COLOR "#FFFFFF"
#define MOUSE_PATH_COLOR "#4287f5"
#define MOUSE_CLICK_COLOR "#f54242"
#define INFO_COLOR "#FFFF00"
#define RECORD_TIME 5

typedef struct {
    double x, y;
    char type; // 'm' - move, 'c' - click
    double time;
} MouseEvent;

typedef struct {
    MouseEvent* events;
    int count;
    int capacity;
} MousePassword;

static const char* group_names[] = {
    "EN", "RU"
};

typedef struct {
    Display* dpy;
    Window win;
    XftFont* font;
    XftDraw* draw;
    XftColor text_color;
    XftColor bg_color;
    XftColor info_color;
    Visual* visual;
    Colormap colormap;
    int screen;
    int width;
    int height;
    Imlib_Image background_image;
    
    int mouse_mode;
    int recording;
    int verifying;
    struct timeval start_time;
    MousePassword password;
    MousePassword reference;
    int attempts;
    char status_msg[128];
    struct timeval status_time;
    int last_x, last_y;
} LockState;

const char* get_current_layout(Display* dpy) {
    XkbStateRec state;
    XkbGetState(dpy, XkbUseCoreKbd, &state);
    
    if (state.group < sizeof(group_names) / sizeof(group_names[0])) {
        return group_names[state.group];
    }
    return "??";
}

double get_elapsed_time(struct timeval start) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
}

void normalize_events(MousePassword* password) {
    if (password->count == 0) return;

    double min_x = DBL_MAX, min_y = DBL_MAX;
    double max_x = -DBL_MAX, max_y = -DBL_MAX;
    for (int i = 0; i < password->count; i++) {
        min_x = fmin(min_x, password->events[i].x);
        min_y = fmin(min_y, password->events[i].y);
        max_x = fmax(max_x, password->events[i].x);
        max_y = fmax(max_y, password->events[i].y);
    }
    
    double width = max_x - min_x;
    double height = max_y - min_y;
    double scale = fmax(width, height);
    if (scale < 1e-5) scale = 1.0;
    
    for (int i = 0; i < password->count; i++) {
        password->events[i].x = (password->events[i].x - min_x) / scale;
        password->events[i].y = (password->events[i].y - min_y) / scale;
    }
}

double compare_passwords(MousePassword* recorded, MousePassword* input) {
    if (recorded->count == 0 || input->count == 0) return 0.0;
    
    double position_score = 0.0;
    double type_score = 0.0;
    int min_count = recorded->count < input->count ? recorded->count : input->count;
    
    for (int i = 0; i < min_count; i++) {
        double dx = recorded->events[i].x - input->events[i].x;
        double dy = recorded->events[i].y - input->events[i].y;
        double dist = sqrt(dx*dx + dy*dy);
        position_score += 1.0 - fmin(1.0, dist);
        
        if (recorded->events[i].type == input->events[i].type) {
            type_score += 1.0;
        }
    }
    
    position_score /= min_count;
    type_score /= min_count;
    
    return 0.7 * position_score + 0.3 * type_score;
}

void save_mouse_password(MousePassword* password) {
    FILE* f = fopen("mouse_password.dat", "w");
    if (!f) return;
    
    fprintf(f, "%d\n", password->count);
    for (int i = 0; i < password->count; i++) {
        fprintf(f, "%c %.6f %.6f %.6f\n", 
               password->events[i].type,
               password->events[i].time,
               password->events[i].x,
               password->events[i].y);
    }
    fclose(f);
}

int load_mouse_password(MousePassword* password) {
    FILE* f = fopen("mouse_password.dat", "r");
    if (!f) return 0;
    
    int count;
    if (fscanf(f, "%d\n", &count) != 1) {
        fclose(f);
        return 0;
    }
    
    password->count = count;
    password->capacity = count + 10;
    password->events = malloc(password->capacity * sizeof(MouseEvent));
    
    for (int i = 0; i < password->count; i++) {
        if (fscanf(f, "%c %lf %lf %lf\n", 
                  &password->events[i].type,
                  &password->events[i].time,
                  &password->events[i].x,
                  &password->events[i].y) != 4) {
            password->count = i;
            break;
        }
    }
    fclose(f);
    return 1;
}

void set_status_message(LockState* s, const char* msg) {
    strncpy(s->status_msg, msg, sizeof(s->status_msg) - 1);
    s->status_msg[sizeof(s->status_msg) - 1] = '\0';
    gettimeofday(&s->status_time, NULL);
}

void start_mouse_recording(LockState* s) {
    s->recording = 1;
    s->password.count = 0;
    s->last_x = -1;
    s->last_y = -1;
    gettimeofday(&s->start_time, NULL);
    set_status_message(s, "Запись нового пароля мышью...");
}

void start_mouse_verification(LockState* s) {
    s->verifying = 1;
    s->password.count = 0;
    s->last_x = -1;
    s->last_y = -1;
    gettimeofday(&s->start_time, NULL);
    set_status_message(s, "Введите пароль мышью...");
}

void add_mouse_event(LockState* s, char type, int x, int y) {
    if (!(s->recording || s->verifying)) return;
    
    double elapsed = get_elapsed_time(s->start_time);
    if (elapsed >= RECORD_TIME) return;
    
    // Игнорировать повторные события на том же месте
    if (type == 'm' && s->last_x == x && s->last_y == y) return;
    
    if (s->password.count >= s->password.capacity) {
        s->password.capacity += 50;
        s->password.events = realloc(s->password.events, s->password.capacity * sizeof(MouseEvent));
    }
    
    MouseEvent event = {
        .x = x,
        .y = y,
        .type = type,
        .time = elapsed
    };
    s->password.events[s->password.count++] = event;
    
    s->last_x = x;
    s->last_y = y;
}

void init_x11(LockState* s) {
    s->dpy = XOpenDisplay(NULL);
    s->screen = DefaultScreen(s->dpy);
    s->width = DisplayWidth(s->dpy, s->screen);
    s->height = DisplayHeight(s->dpy, s->screen);

    s->visual = DefaultVisual(s->dpy, s->screen);
    s->colormap = DefaultColormap(s->dpy, s->screen);

    XSetWindowAttributes attrs = {
        .override_redirect = True,
        .background_pixel = BlackPixel(s->dpy, s->screen),
        .border_pixel = BlackPixel(s->dpy, s->screen)
    };

    s->win = XCreateWindow(s->dpy, RootWindow(s->dpy, s->screen),
        0, 0, s->width, s->height, 0,
        CopyFromParent, InputOutput, s->visual,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel,
        &attrs);

    XMapWindow(s->dpy, s->win);
    XGrabPointer(s->dpy, s->win, True, PointerMotionMask | ButtonPressMask, 
                GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(s->dpy, s->win, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    s->font = XftFontOpenName(s->dpy, s->screen, FONT_NAME);
    if (!s->font) {
        fprintf(stderr, "Ошибка загрузки шрифта: %s\n", FONT_NAME);
        exit(1);
    }
    s->draw = XftDrawCreate(s->dpy, s->win, s->visual, s->colormap);

    XftColorAllocName(s->dpy, s->visual, s->colormap, TEXT_COLOR, &s->text_color);
    XftColorAllocName(s->dpy, s->visual, s->colormap, BACKGROUND_COLOR, &s->bg_color);
    XftColorAllocName(s->dpy, s->visual, s->colormap, INFO_COLOR, &s->info_color);

    s->background_image = imlib_load_image("/tmp/screen1.png");
    if (s->background_image) {
        imlib_context_set_image(s->background_image);
        imlib_context_set_display(s->dpy);
        imlib_context_set_visual(s->visual);
        imlib_context_set_colormap(s->colormap);
        imlib_context_set_drawable(s->win);
    }
    
    // Инициализация мышиного пароля
    s->password.capacity = 100;
    s->password.events = malloc(s->password.capacity * sizeof(MouseEvent));
    s->password.count = 0;
    
    s->reference.capacity = 0;
    s->reference.count = 0;
    s->reference.events = NULL;
    if (load_mouse_password(&s->reference)) {
        printf("Загружен пароль из файла, точек: %d\n", s->reference.count);
    } else {
        s->reference.events = malloc(sizeof(MouseEvent));
        s->reference.capacity = 1;
        printf("Пароль не загружен, создан пустой\n");
    }
    s->mouse_mode = 0;
    s->recording = 0;
    s->verifying = 0;
    s->attempts = 0;
    s->status_msg[0] = '\0';
    s->last_x = -1;
    s->last_y = -1;
}

void draw_centered_text(LockState* s, const char* text, int y_offset, XftColor* color) {
    if (!text || !*text) return;
    
    XGlyphInfo extents;
    XftTextExtentsUtf8(s->dpy, s->font, (FcChar8*)text, strlen(text), &extents);

    int x = (s->width - extents.width) / 2;
    int y = (s->height - extents.height) / 2 + y_offset;

    XftDrawStringUtf8(s->draw, color, s->font, x, y, 
                     (FcChar8*)text, strlen(text));
}

void draw_password_dots(LockState* s, int count) {
    if (s->mouse_mode) return;
    
    int total_width = (DOT_RADIUS * 2 + DOT_SPACING) * count - DOT_SPACING;
    int start_x = (s->width - total_width) / 2;
    int y = s->height / 2 + 50;

    for (int i = 0; i < count; i++) {
        XFillArc(s->dpy, s->win, DefaultGC(s->dpy, s->screen),
                start_x + i * (DOT_RADIUS * 2 + DOT_SPACING), y - DOT_RADIUS,
                DOT_RADIUS * 2, DOT_RADIUS * 2, 0, 360 * 64);
    }
}

void draw_time(LockState* s) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    draw_centered_text(s, time_str, -100, &s->text_color);
}

void draw_keyboard_layout(LockState* s) {
    const char* layout = get_current_layout(s->dpy);
    XGlyphInfo extents;
    
    XftTextExtentsUtf8(s->dpy, s->font, (FcChar8*)layout, strlen(layout), &extents);
    
    int x = 20;
    int y = extents.height + 20;
    
    XftDrawStringUtf8(s->draw, &s->text_color, s->font, x, y, 
                     (FcChar8*)layout, strlen(layout));
}

void draw_background(LockState* s) {
    if (s->background_image) {
        imlib_context_set_image(s->background_image);
        imlib_render_image_on_drawable(0, 0);
    } else {
        XClearWindow(s->dpy, s->win);
    }
}

void draw_mouse_path(LockState* s) {
    if (s->password.count < 1) return;
    
    GC gc = XCreateGC(s->dpy, s->win, 0, NULL);
    XColor path_color;
    XParseColor(s->dpy, s->colormap, MOUSE_PATH_COLOR, &path_color);
    XAllocColor(s->dpy, s->colormap, &path_color);
    XSetForeground(s->dpy, gc, path_color.pixel);
    XSetLineAttributes(s->dpy, gc, 3, LineSolid, CapRound, JoinRound);
    
    // Рисуем всю траекторию
    for (int i = 1; i < s->password.count; i++) {
        if (s->password.events[i].type == 'm' && s->password.events[i-1].type == 'm') {
            XDrawLine(s->dpy, s->win, gc,
                     (int)s->password.events[i-1].x, (int)s->password.events[i-1].y,
                     (int)s->password.events[i].x, (int)s->password.events[i].y);
        }
    }
    
    // Рисуем клики
    XColor click_color;
    XParseColor(s->dpy, s->colormap, MOUSE_CLICK_COLOR, &click_color);
    XAllocColor(s->dpy, s->colormap, &click_color);
    XSetForeground(s->dpy, gc, click_color.pixel);
    
    for (int i = 0; i < s->password.count; i++) {
        if (s->password.events[i].type == 'c') {
            XFillArc(s->dpy, s->win, gc,
                    (int)s->password.events[i].x - DOT_RADIUS,
                    (int)s->password.events[i].y - DOT_RADIUS,
                    DOT_RADIUS * 2, DOT_RADIUS * 2, 0, 360 * 64);
        }
    }
    
    XFreeGC(s->dpy, gc);
}

void redraw_screen(LockState* s, int pass_len) {
    draw_background(s);
    draw_time(s);
    draw_keyboard_layout(s);

    if (s->mouse_mode) {
        draw_centered_text(s, "ВВОДИТЕ ПАРОЛЬ МЫШЬЮ", -30, &s->text_color);
        draw_mouse_path(s);  //print mouse path!!!
        
        if (s->recording || s->verifying) {
            double elapsed = get_elapsed_time(s->start_time);
            double time_left = RECORD_TIME - elapsed;
            char time_msg[50];
            snprintf(time_msg, sizeof(time_msg), "Осталось: %.1f сек", time_left);
            draw_centered_text(s, time_msg, 20, &s->text_color);
        }
    } else {
        draw_centered_text(s, "СИСТЕМА ЗАБЛОКИРОВАНА", -30, &s->text_color);
        draw_password_dots(s, pass_len);
    }

    if (s->attempts > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Неверный пароль (%d попытка)", s->attempts);
        draw_centered_text(s, buf, 50, &s->text_color);
    }

    // Отображаем статусное сообщение
    if (s->status_msg[0] != '\0') {
        double elapsed = get_elapsed_time(s->status_time);
        if (elapsed < 3.0) {
            draw_centered_text(s, s->status_msg, 100, &s->info_color);
        } else {
            s->status_msg[0] = '\0';
        }
    }

    XFlush(s->dpy);
}

void save_reference_password(LockState* s) {
    // Освобождаем старые события
    if (s->reference.events) {
        free(s->reference.events);
    }
    
    // Копируем текущий пароль
    s->reference.count = s->password.count;
    s->reference.capacity = s->password.count + 10;
    s->reference.events = malloc(s->reference.capacity * sizeof(MouseEvent));
    memcpy(s->reference.events, s->password.events, 
          s->password.count * sizeof(MouseEvent));
    
    // Сохраняем в файл
    save_mouse_password(&s->reference);
    printf("Пароль сохранен, точек: %d\n", s->reference.count);
}

void lock_loop(LockState* s) {
    XEvent ev;
    char pass[64] = {0};
    int pass_len = 0;
    s->attempts = 0;
    time_t last_time = 0;

    while (1) {
        time_t current_time = time(NULL);
        if (current_time != last_time) {
            last_time = current_time;
            redraw_screen(s, pass_len);
        }

        if (XPending(s->dpy) > 0) {
            XNextEvent(s->dpy, &ev);
            
            switch (ev.type) {
                case MotionNotify:
                    if (s->recording || s->verifying) {
                        add_mouse_event(s, 'm', ev.xmotion.x, ev.xmotion.y);
                        // Перерисовка для плавного отображения
                        redraw_screen(s, pass_len);
                    }
                    break;
                    
                case ButtonPress:
                    if (s->recording || s->verifying) {
                        add_mouse_event(s, 'c', ev.xbutton.x, ev.xbutton.y);
                        redraw_screen(s, pass_len);
                    }
                    break;
                    
                case KeyPress:
                    KeySym keysym;
                    char buffer[8];
                    int len = XLookupString(&ev.xkey, buffer, sizeof(buffer), &keysym, NULL);

                    if (keysym == XK_Return) {
                        if (s->mouse_mode) {
                            // Завершение мышиного ввода
                            if (s->recording) {
                                normalize_events(&s->password);
                                save_reference_password(s);
                                s->recording = 0;
                                set_status_message(s, "Пароль сохранен!");
                            } else if (s->verifying) {
                                normalize_events(&s->password);
                                double similarity = compare_passwords(&s->reference, &s->password);
                                printf("Сходство паролей: %.2f\n", similarity);
                                
                                if (similarity > 0.90) { //!!! compare pass
                                    goto unlock;
                                } else {
                                    s->attempts++;
                                    set_status_message(s, "Пароль неверный!");
                                }
                                s->verifying = 0;
                            }
                        } else {
                            // Проверка клавиатурного пароля
                            if (strcmp(pass, "123") == 0) goto unlock;
                            s->attempts++;
                            pass[0] = '\0';
                            pass_len = 0;
                            set_status_message(s, "Неверный пароль!");
                        }
                    } else if (keysym == XK_BackSpace && pass_len > 0) {
                        pass[--pass_len] = '\0';
                    } else if (keysym == XK_F2) {
                        // Переключение в режим мышиного пароля
                        s->mouse_mode = !s->mouse_mode;
                        if (s->mouse_mode) {
                            if (s->reference.count > 0) {
                                start_mouse_verification(s);
                            } else {
                                set_status_message(s, "Пароль не записан! Нажмите F3");
                            }
                        } else {
                            set_status_message(s, "Режим клавиатурного ввода");
                        }
                        s->password.count = 0;
                    } else if (keysym == XK_F3) {
                        // Запись нового мышиного пароля
                        s->mouse_mode = 1; //!!! delete for security
                        start_mouse_recording(s);
                    } else if (pass_len < sizeof(pass) - 1 && len > 0) {
                        pass[pass_len++] = buffer[0];
                        pass[pass_len] = '\0';
                    }
                    break;
            }
        } else {
            // Проверка таймаута мышиного ввода
            if ((s->recording || s->verifying) && 
                get_elapsed_time(s->start_time) >= RECORD_TIME) {
                
                if (s->recording) {
                    normalize_events(&s->password);
                    save_reference_password(s);
                    s->recording = 0;
                    set_status_message(s, "Пароль сохранен автоматически");
                } else if (s->verifying) {
                    normalize_events(&s->password);
                    double similarity = compare_passwords(&s->reference, &s->password);
                    printf("Авто-проверка, сходство: %.2f\n", similarity);
                    
                    if (similarity > 0.90) { //!!!!pass compare
                        goto unlock;
                    } else {
                        s->attempts++;
                        set_status_message(s, "Пароль неверный!");
                    }
                    s->verifying = 0;
                }
                s->password.count = 0;
            }
            usleep(10000);
        }
    }
    
unlock:
    XUngrabKeyboard(s->dpy, CurrentTime);
    XUngrabPointer(s->dpy, CurrentTime);
}

int main() {
    setlocale(LC_ALL, "");

    LockState state = {0};
    init_x11(&state);
    lock_loop(&state);

    XDestroyWindow(state.dpy, state.win);
    XCloseDisplay(state.dpy);

    if (state.background_image) {
        imlib_context_set_image(state.background_image);
        imlib_free_image();
    }

    free(state.password.events);
    if (state.reference.events) {
        free(state.reference.events);
    }

    return 0;
}
