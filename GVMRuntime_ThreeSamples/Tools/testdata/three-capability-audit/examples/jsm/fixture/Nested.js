/** Creates an auxiliary point object with a compute signal for audit tests. */
export function createNestedPoint() {

	const points = new THREE.Points();
	points.computeAsync();
	return points;

}
