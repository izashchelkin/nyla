// Test fixture for xcav -- JavaScript
import { helper } from './utils';

export function greet(name) {
    return `Hello, ${name}!`;
}

const add = (a, b) => a + b;

class Calculator {
    multiply(x, y) {
        return x * y;
    }

    divide(x, y) {
        return x / y;
    }
}

export default greet;
