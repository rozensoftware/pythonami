# Language Level 0.8: classes, instances, single inheritance

class Animal:
    kind = "creature"

    def __init__(self, name):
        self.name = name

    def greet(self):
        return self.name


class Dog(Animal):
    def greet(self):
        return self.name


class E(Exception):
    pass


d = Dog("x")
print(d.greet())
print(d.kind)
print(isinstance(d, Animal))
print(isinstance(d, Dog))
print(issubclass(Dog, Animal))
print(issubclass(Dog, object))

try:
    raise E("boom")
except E as e:
    print(e.message)

try:
    raise ValueError("bad")
except Exception:
    print("caught")

try:
    raise E("x")
except Exception:
    print("user-exc")
