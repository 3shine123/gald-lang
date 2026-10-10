#ifndef JETI_HELLO_NP_H
#define JETI_HELLO_NP_H

#include <jeti/object.h>

struct jeti_NPObject_vtable;
struct jeti_Student_vtable;

struct NPObject;
NPObject * NPObject_init(NPObject * self, SEL _cmd);
void NPObject_dealloc(NPObject * self, SEL _cmd);
struct jeti_NPObject_vtable;
struct Student;
int Student_grade(NPObject * self, SEL _cmd);
void Student_setGrade_(NPObject * self, SEL _cmd, int value);
struct jeti_Student_vtable;
extern NPClass jeti_NPObject_class;
extern NPClass jeti_Student_class;
void jeti_init(void);

#endif /* JETI_HELLO_NP_H */
