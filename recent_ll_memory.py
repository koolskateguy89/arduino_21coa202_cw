import subprocess

# used this file to generate data for graphs

START = 0
END = 68 + 1
NUM_VALUES = END - START

def llMemory():
  def memoryForLL(num: int):
    if num > 64:
      num = 64
    return num * 5

  memory = [memoryForLL(i) for i in range(START, END)]
  return str(memory)[1:-1]

def arrayMemory():
  def memoryForArray(num: int):
    if num == 0:
      return 0
    return 64

  memory = [memoryForArray(i) for i in range(START, END)]
  return str(memory)[1:-1]

def xAxis():
  values = [i for i in range(START, END)]
  return str(values)[1:-1]

if __name__ == '__main__':
  ll = llMemory()
  arr = arrayMemory()
  xaxis = xAxis()

  for copy in [ll, arr, xaxis]:
    input("press enter to copy")
    copy = copy.replace(',', '')
    print(copy)
    subprocess.run("clip", universal_newlines=True, input=copy)
    ...
