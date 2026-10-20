// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

// Places each box's number in a margin outside the screenshot, joined to its box by a leader line, so a
// number never covers the UI. All values are in image pixels; the figure is scaled as a whole.
//
// Each box takes the nearest image edge whose path to it isn't blocked by another box. The leader runs
// straight out from the box through that unblocked path to the image edge, then on to the number, which
// is nudged along the margin so it doesn't collide with its neighbours. A long leader is hard to follow,
// so a box with no short, unblocked path has its number on the box instead: at its corner, or just
// outside a tiny box so it doesn't hide it.

const k_sides = ['left', 'right', 'top', 'bottom'];

const IsVerticalSide = (side) => side === 'left' || side === 'right';

const Contains = (outer, inner) =>
    outer.x <= inner.x &&
    outer.y <= inner.y &&
    outer.x + outer.w >= inner.x + inner.w &&
    outer.y + outer.h >= inner.y + inner.h;

const Clamp = (value, lo, hi) => Math.min(Math.max(value, lo), hi);

function AlongRange(box, side) {
    return IsVerticalSide(side) ? [box.y, box.y + box.h] : [box.x, box.x + box.w];
}

function DepthRange(box, side) {
    return IsVerticalSide(side) ? [box.x, box.x + box.w] : [box.y, box.y + box.h];
}

function Depth(box, side, image) {
    switch (side) {
        case 'left': return box.x;
        case 'right': return image.width - (box.x + box.w);
        case 'top': return box.y;
        case 'bottom': return image.height - (box.y + box.h);
    }
}

// The parts of the box's extent along an edge through which a straight leader can reach that edge
// without crossing another box. Boxes nested inside one another don't block each other.
function FreeIntervals(box_index, side, boxes, image) {
    const box = boxes[box_index];
    const corridor_depth =
        side === 'left' || side === 'top'
            ? [0, DepthRange(box, side)[0]]
            : [DepthRange(box, side)[1], IsVerticalSide(side) ? image.width : image.height];
    const k_clearance = 2;

    let intervals = [AlongRange(box, side)];
    boxes.forEach((other, other_index) => {
        if (other_index === box_index || Contains(other, box) || Contains(box, other)) return;
        const [other_depth_lo, other_depth_hi] = DepthRange(other, side);
        if (other_depth_hi <= corridor_depth[0] || other_depth_lo >= corridor_depth[1]) return;
        const [blocked_lo, blocked_hi] = AlongRange(other, side);
        intervals = intervals.flatMap(([lo, hi]) => {
            if (blocked_hi + k_clearance <= lo || blocked_lo - k_clearance >= hi) return [[lo, hi]];
            return [
                [lo, blocked_lo - k_clearance],
                [blocked_hi + k_clearance, hi],
            ].filter(([a, b]) => b > a);
        });
    });

    const along_length = AlongRange(box, side)[1] - AlongRange(box, side)[0];
    const k_min_length = Math.min(6, along_length);
    return intervals.filter(([lo, hi]) => hi - lo >= k_min_length);
}

// The point within the free intervals closest to the given position along the edge.
function NearestFreePoint(intervals, along) {
    let best = null;
    for (const [lo, hi] of intervals) {
        const inset = Math.min(3, (hi - lo) / 2);
        const point = Clamp(along, lo + inset, hi - inset);
        if (best === null || Math.abs(point - along) < Math.abs(best - along)) best = point;
    }
    return best;
}

// Each box's margin side, or null when its number goes on the box.
function ChooseSides(boxes, image) {
    const max_depth = 0.12 * Math.max(image.width, image.height);
    const options = boxes.map((box, box_index) =>
        k_sides
            .map((side) => ({
                side,
                depth: Depth(box, side, image),
                intervals: FreeIntervals(box_index, side, boxes, image),
            }))
            .filter((option) => option.intervals.length && option.depth <= max_depth)
            .sort((a, b) => a.depth - b.depth)
    );

    const chosen = options.map((box_options) => box_options[0] ?? null);

    // A margin used by only one number costs a whole strip of space; move that number to a margin
    // that's already used if it isn't much further away.
    const k_max_extra_depth_fraction = 0.25;
    for (const [box_index, box_options] of options.entries()) {
        if (!chosen[box_index]) continue;
        const side = chosen[box_index].side;
        if (chosen.filter((option) => option?.side === side).length !== 1) continue;
        const dimension = IsVerticalSide(side) ? image.width : image.height;
        const alternative = box_options.find(
            (option) =>
                option.side !== side &&
                chosen.some((other) => other?.side === option.side) &&
                option.depth <= chosen[box_index].depth + k_max_extra_depth_fraction * dimension
        );
        if (alternative) chosen[box_index] = alternative;
    }
    return chosen;
}

// Spreads positions along a line so they're at least `pitch` apart, keeping each group of
// neighbours centred on where its members want to be.
function SpreadAlong(targets, pitch, lo, hi) {
    const order = targets.map((_, index) => index).sort((a, b) => targets[a] - targets[b]);
    const Position = (cluster) => {
        const members = order.slice(cluster.start, cluster.start + cluster.count);
        const mean = members.reduce((sum, index) => sum + targets[index], 0) / cluster.count;
        const span = (cluster.count - 1) * pitch;
        return Math.max(lo, Math.min(mean - span / 2, hi - span));
    };

    let clusters = order.map((_, start) => ({ start, count: 1 }));
    clusters.forEach((cluster) => (cluster.pos = Position(cluster)));
    for (let merged = true; merged; ) {
        merged = false;
        for (let cluster_index = 0; cluster_index + 1 < clusters.length; ++cluster_index) {
            const cluster = clusters[cluster_index];
            const next = clusters[cluster_index + 1];
            if (next.pos >= cluster.pos + cluster.count * pitch - 1e-6) continue;
            const combined = { start: cluster.start, count: cluster.count + next.count };
            combined.pos = Position(combined);
            clusters.splice(cluster_index, 2, combined);
            merged = true;
            break;
        }
    }

    const positions = new Array(targets.length);
    for (const cluster of clusters)
        for (let offset = 0; offset < cluster.count; ++offset)
            positions[order[cluster.start + offset]] = cluster.pos + offset * pitch;
    return positions;
}

// A tiny box would be hidden under its number, so the number sits just outside the corner that faces
// the middle of the image, where there's more likely to be room.
function OnBoxBadgeCentre(box, image, badge_diameter) {
    const radius = badge_diameter / 2;
    if (Math.max(box.w, box.h) >= badge_diameter * 3)
        return { x: box.x + radius * 0.4, y: box.y + radius * 0.4 };
    const outside = radius + badge_diameter * 0.1;
    const faces_left = box.x + box.w / 2 > image.width / 2;
    const faces_up = box.y + box.h / 2 > image.height / 2;
    return {
        x: faces_left ? box.x - outside : box.x + box.w + outside,
        y: faces_up ? box.y - outside : box.y + box.h + outside,
    };
}

/**
 * @param {{width: number, height: number}} image - Image size in pixels
 * @param {{x: number, y: number, w: number, h: number}[]} boxes - Box rects in image pixels
 * @param {number} badge_diameter - Size of a number's circle in image pixels
 * @returns {{
 *   margins: {left: number, right: number, top: number, bottom: number},
 *   width: number, height: number,
 *   callouts: {badge: Point, leader: Point[] | null}[],
 * }} Figure size including margins; each box's number centre and its leader's points, all in figure
 *   pixels. A number on its box has no leader.
 */
export function LayoutCallouts({ image, boxes, badge_diameter }) {
    const radius = badge_diameter / 2;
    const gap = badge_diameter * 0.6;
    const edge_padding = badge_diameter * 0.15;
    const margin_size = edge_padding + badge_diameter + gap;
    const pitch = badge_diameter * 1.15;

    const chosen = ChooseSides(boxes, image);
    const margins = Object.fromEntries(
        k_sides.map((side) => [side, chosen.some((option) => option?.side === side) ? margin_size : 0])
    );
    const width = margins.left + image.width + margins.right;
    const height = margins.top + image.height + margins.bottom;

    const badge_along = new Array(boxes.length);
    for (const side of k_sides) {
        const members = chosen.flatMap((option, box_index) => (option?.side === side ? [box_index] : []));
        if (!members.length) continue;
        const targets = members.map((box_index) => {
            const [lo, hi] = AlongRange(boxes[box_index], side);
            return NearestFreePoint(chosen[box_index].intervals, (lo + hi) / 2);
        });
        // Numbers stay alongside the image unless there are too many to fit, then spill into the corners.
        const image_length = IsVerticalSide(side) ? image.height : image.width;
        const offset = IsVerticalSide(side) ? margins.top : margins.left;
        const total_length = IsVerticalSide(side) ? height : width;
        const fits = members.length * pitch <= image_length;
        const lo = fits ? radius : radius + edge_padding - offset;
        const hi = fits ? image_length - radius : total_length - radius - edge_padding - offset;
        const positions = SpreadAlong(targets, pitch, lo, hi);
        members.forEach((box_index, member_index) => (badge_along[box_index] = positions[member_index]));
    }

    const callouts = boxes.map((box, box_index) => {
        if (!chosen[box_index]) {
            const badge = OnBoxBadgeCentre(box, image, badge_diameter);
            return { badge: { x: margins.left + badge.x, y: margins.top + badge.y }, leader: null };
        }
        const side = chosen[box_index].side;
        const along = badge_along[box_index];
        const anchor = NearestFreePoint(chosen[box_index].intervals, along);
        const ToFigure = (depth_coord, along_coord) =>
            IsVerticalSide(side)
                ? { x: margins.left + depth_coord, y: margins.top + along_coord }
                : { x: margins.left + along_coord, y: margins.top + depth_coord };

        const [depth_lo, depth_hi] = DepthRange(box, side);
        const near_edge = side === 'left' || side === 'top';
        const box_point = ToFigure(near_edge ? depth_lo : depth_hi, anchor);
        const image_edge_depth = near_edge ? 0 : IsVerticalSide(side) ? image.width : image.height;
        const image_edge_point = ToFigure(image_edge_depth, anchor);
        const badge_depth = near_edge ? -(gap + radius) : image_edge_depth + gap + radius;
        const badge = ToFigure(badge_depth, along);

        const leader =
            Math.abs(anchor - along) < 0.5 ? [box_point, badge] : [box_point, image_edge_point, badge];
        return { badge, leader };
    });

    return { margins, width, height, callouts };
}
