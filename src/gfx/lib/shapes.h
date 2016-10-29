#ifndef SHAPES_H
#define SHAPES_H

#include "gfx.h"

typedef struct line {
	Coordinate p1;
	Coordinate p2;
} Line;

typedef struct circle {
	Coordinate center;
	int radius;
} Circle;

typedef struct triangle {
	Coordinate p1;
	Coordinate p2;
	Coordinate p3;
} Triangle;

#define rect_min_x(r) ((r).origin.x)
#define rect_min_y(r) ((r).origin.y)
#define rect_max_x(r) ((r).origin.x + (r).size.width)
#define rect_max_y(r) ((r).origin.y + (r).size.height)

void normalize_coordinate(ca_layer* layer, Coordinate* p);

Coordinate point_make(int x, int y);
Size size_make(int w, int h);
Rect rect_make(Coordinate origin, Size size);
Line line_make(Coordinate p1, Coordinate p2);
Circle circle_make(Coordinate center, int radius);
Triangle triangle_make(Coordinate p1, Coordinate p2, Coordinate p3);

#define THICKNESS_FILLED -1

void draw_rect(ca_layer* layer, Rect rect, Color color, int thickness);
void draw_line(ca_layer* layer, Line line, Color color, int thickness);
void draw_triangle(ca_layer* layer, Triangle triangle, Color color, int thickness);
void draw_circle(ca_layer* layer, Circle circle, Color color, int thickness);
#endif
