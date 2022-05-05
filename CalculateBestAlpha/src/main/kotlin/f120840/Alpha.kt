package f120840

import kotlin.math.absoluteValue

// to see difference when using total average
const val TOTAL_AVERAGE = 0

fun main(vararg args: String) {
    val results = mutableMapOf<Int, MutableList<Double>>()

    repeat(1_000) {
        val a = Alpha(300)
        a()
        a.avrgDiffResults.forEach { (alpha, diff) ->
            results.computeIfAbsent(alpha) { mutableListOf() }
                .add(diff)
        }
    }

    results.mapValues { (alpha, diffs) -> diffs.average() }
        .apply {
            toSortedMap { d1, d2 ->
                // sort by ascending avrg (best avrg diff first)
                val a1 = this[d1]!!
                val a2 = this[d2]!!
                a1.compareTo(a2)
            }.forEach { (alpha, avrgDiff) ->
                if (alpha == TOTAL_AVERAGE)
                    println(" TOTAL  =  $avrgDiff".format(alpha))
                else
                    println("1 / %-2d  =  $avrgDiff".format(alpha))
            }
        }
}

class Alpha(val nRecents: Int) {
    val alphas = (2..64) + TOTAL_AVERAGE

    val alphasAvrgs = mutableMapOf<Int, Double>().apply {
        alphas.forEach {
            this[it] = 0.0
        }
    }
    val results = mutableMapOf<Int, MutableList<Double>>()
    val avrgDiffResults = mutableMapOf<Int, Double>()

    private val randomByte: Int
        get() = (0..255).random()

    val values = mutableListOf<Int>().apply {
        repeat(64) {
            add(randomByte)
        }
    }

    operator fun invoke() {
        val startAvrg = values.average()
        alphas.forEach {
            alphasAvrgs[it] = startAvrg
        }
        alphasAvrgs[TOTAL_AVERAGE] = values.sum().toDouble()

        for (i in 65..nRecents) {
            val added = randomByte.also { values.add(it) }
            val exact = values.averageLast(64)

            alphas.forEach {
                val diff: Double
                if (it == TOTAL_AVERAGE) {
                    val sum: Double = alphasAvrgs.getValue(it) + added
                    alphasAvrgs[it] = sum

                    val avrg: Double = sum / values.size

                     diff = (exact - avrg).absoluteValue
                } else {
                    val alpha: Double = 1.0 / it

                    var runningAvrg = alphasAvrgs.getValue(it)
                    runningAvrg = alpha * added + (1 - alpha) * runningAvrg
                    alphasAvrgs[it] = runningAvrg

                    diff = (exact - runningAvrg).absoluteValue
                }

                results.computeIfAbsent(it) { mutableListOf() }
                    .add(diff)
            }
        }

        // calculate avrgDiffResults
        results.forEach { (alpha, diffs) ->
            avrgDiffResults[alpha] = diffs.average()
        }
    }
}

fun List<Int>.averageLast(n: Int): Double {
    var sum: Double = 0.0
    var count: Int = 0

    for (i in this.lastIndex downTo this.lastIndex - n + 1) {
        sum += this[i]
        count++
    }
    return if (count == 0) Double.NaN else sum / count
}
