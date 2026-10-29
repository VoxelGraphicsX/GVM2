// Initialize the page once before replay.  The consent overlay removes its
// button, so install a harmless same-id placeholder for the replay's original
// consent event; the second replay event still toggles the real checkbox.
const startButton = document.querySelector('#startButton');
if (!startButton) throw new Error('Glitch reference start button is missing.');
startButton.click();
const replayButton = document.createElement('button');
replayButton.id = 'startButton';
replayButton.type = 'button';
replayButton.style.display = 'none';
document.body.appendChild(replayButton);
