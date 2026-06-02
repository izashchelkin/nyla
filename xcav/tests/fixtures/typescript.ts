// Test fixture for xcav -- TypeScript
import { strict as assert } from 'assert';

interface Shape {
    area(): number;
    perimeter(): number;
}

type Point = { x: number; y: number };

class Circle implements Shape {
    constructor(private radius: number) {}

    area(): number {
        return Math.PI * this.radius ** 2;
    }

    perimeter(): number {
        return 2 * Math.PI * this.radius;
    }
}

function distance(a: Point, b: Point): number {
    return Math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2);
}
