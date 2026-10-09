#ifndef OVIC_HELLO_NP_H
#define OVIC_HELLO_NP_H

#include <ovic/object.h>

struct ovic_NPObject_vtable;
struct ovic_Student_vtable;

struct NPObject;
NPObject * NPObject_init(NPObject * self, SEL _cmd);
void NPObject_dealloc(NPObject * self, SEL _cmd);
struct ovic_NPObject_vtable;
struct Student;
int Student_grade(NPObject * self, SEL _cmd);
void Student_setGrade_(NPObject * self, SEL _cmd, int value);
struct ovic_Student_vtable;
extern NPClass ovic_NPObject_class;
extern NPClass ovic_Student_class;
void ovic_init(void);

#endif /* OVIC_HELLO_NP_H */
