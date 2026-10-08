#ifndef NEPA_HELLO_NP_H
#define NEPA_HELLO_NP_H

#include <nepa/object.h>

struct nepa_NPObject_vtable;
struct nepa_Student_vtable;

struct NPObject;
NPObject * NPObject_init(NPObject * self, SEL _cmd);
void NPObject_dealloc(NPObject * self, SEL _cmd);
struct nepa_NPObject_vtable;
struct Student;
int Student_grade(NPObject * self, SEL _cmd);
void Student_setGrade_(NPObject * self, SEL _cmd, int value);
struct nepa_Student_vtable;
extern NPClass nepa_NPObject_class;
extern NPClass nepa_Student_class;
void nepa_init(void);

#endif /* NEPA_HELLO_NP_H */
