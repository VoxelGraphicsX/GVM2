const slider = document.querySelector('.slider');
if (!(slider instanceof HTMLElement)) {
  throw new Error('The multiple-scenes slider element was not found.');
}
const dispatchPointer = (type, clientX) => slider.dispatchEvent(new PointerEvent(type, {
  bubbles: true,
  pointerId: 91,
  isPrimary: true,
  pointerType: 'mouse',
  button: 0,
  buttons: type === 'pointerup' ? 0 : 1,
  clientX,
  clientY: 250
}));
dispatchPointer('pointerdown', 400);
for (let clientX = 380; clientX >= 200; clientX -= 20) {
  dispatchPointer('pointermove', clientX);
}
dispatchPointer('pointerup', 200);
