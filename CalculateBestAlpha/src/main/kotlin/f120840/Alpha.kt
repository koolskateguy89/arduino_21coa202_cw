package f120840

import kotlin.math.absoluteValue

fun main(vararg args: String) {
    val results = mutableMapOf<Int, MutableList<Double>>()

    repeat(1_000) {
        println(it)
        val a = Alpha(300)
        a()
        a.avrgDiffResults.forEach { (alpha, diff) ->
            // println("1 / $alpha  =   $diff")
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
                println("1 / %-2d  =  $avrgDiff".format(alpha))
            }
        }
}

class Alpha(val nRecents: Int) {
    val alphas = 2..64
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

        for (i in 65..nRecents) {
            println("\t$i")
            // println(values.takeLast(64))
            val added = randomByte.also { values.add(it) }
            // val exact = values.takeLast(64).average()
            val exact = values.averageLast(64)
            // println("exact = $exact")

            alphas.forEach {
                val alpha: Double = 1.0 / it

                var runningAvrg = alphasAvrgs.getValue(it)
                runningAvrg = alpha * added + (1 - alpha) * runningAvrg
                alphasAvrgs[it] = runningAvrg

                val diff = (exact - runningAvrg).absoluteValue

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