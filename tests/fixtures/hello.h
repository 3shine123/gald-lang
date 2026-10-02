#ifndef GALD_HELLO_NF_H
#define GALD_HELLO_NF_H

#include <gald/object.h>

struct gald_NFObject_vtable;
struct gald_Student_vtable;

struct NFObject;
NFObject * NFObject_init(NFObject * self, SEL _cmd);
void NFObject_dealloc(NFObject * self, SEL _cmd);
struct gald_NFObject_vtable;
struct Student;
int Student_grade(NFObject * self, SEL _cmd);
void Student_setGrade_(NFObject * self, SEL _cmd, int value);
struct gald_Student_vtable;
extern NFClass gald_NFObject_class;
extern NFClass gald_Student_class;
void gald_init(void);

#endif /* GALD_HELLO_NF_H */
