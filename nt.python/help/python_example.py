def multiply(a,b):
	maxobj.outlet("mult", a, b, a * b)
	return a * b

def power(a, b):
	maxobj.outlet("power", a, b, a ** b)
	return a ** b
