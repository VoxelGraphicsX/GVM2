const reduceSelectors = Array.from(
  document.querySelectorAll('.lil-gui .controller.option select'));

if (reduceSelectors.length < 4) {
  throw new Error('The compute-reduce GUI selectors are unavailable.');
}

const selectReduceValue = (selector, value) => {
  selector.value = value;
  selector.dispatchEvent(new Event('change', { bubbles: true }));
};

selectReduceValue(reduceSelectors[0], 'Reduce 2 (Workgroup Reduction)');
selectReduceValue(reduceSelectors[1], 'Reduce 3 (Subgroup Reduce)');
selectReduceValue(reduceSelectors[2], 'Workgroup Sum Grid');
selectReduceValue(reduceSelectors[3], 'Input Element 0');
