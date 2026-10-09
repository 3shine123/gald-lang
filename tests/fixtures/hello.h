#ifndef OVEL_HELLO_NP_H
#define OVEL_HELLO_NP_H

#include <ovel/object.h>

struct ovel_NPObject_vtable;
struct ovel_Student_vtable;

struct NPObject;
NPObject * NPObject_init(NPObject * self, SEL _cmd);
void NPObject_dealloc(NPObject * self, SEL _cmd);
struct ovel_NPObject_vtable;
struct Student;
int Student_grade(NPObject * self, SEL _cmd);
void Student_setGrade_(NPObject * self, SEL _cmd, int value);
struct ovel_Student_vtable;
extern NPClass ovel_NPObject_class;
extern NPClass ovel_Student_class;
void ovel_init(void);

#endif /* OVEL_HELLO_NP_H */
