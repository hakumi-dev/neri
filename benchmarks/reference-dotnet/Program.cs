using System.Diagnostics;

sealed class ComputeNode(long value, ComputeNode? next)
{
    public long Value = value;
    public ComputeNode? Next = next;
}

static class Program
{
    static long Residue(long value) => value - (value / 1000003) * 1000003;

    static long IntegerKernel(long count)
    {
        long value = 1;
        for (long index = 0; index < count; index++)
            value = Residue(value * 17 + 23);
        return value;
    }

    static long IntegerExpected(long count)
    {
        long power = 1, factor = 17;
        for (long exponent = count; exponent > 0; exponent /= 2)
        {
            if (exponent - (exponent / 2) * 2 == 1)
                power = Residue(power * factor);
            factor = Residue(factor * factor);
        }
        return Residue(power + 23 * (power - 1) * 312501);
    }

    static long ArrayKernel(long[] values, long rounds)
    {
        long sum = 0;
        for (long round = 0; round < rounds; round++)
            for (long index = 0; index < values.LongLength; index++)
            {
                values[index] = values[index] + 1;
                sum += values[index];
            }
        return sum;
    }

    static long BatchKernel(long size, long jobs, int workers)
    {
        long sum = 0;
        if (workers == 1)
        {
            for (long job = 0; job < jobs; job++)
                sum += IntegerKernel(size + job);
        }
        else
        {
            var results = new long[(int)jobs];
            Parallel.For(0, (int)jobs, new ParallelOptions { MaxDegreeOfParallelism = workers },
                job => results[job] = IntegerKernel(size + job));
            foreach (long value in results)
                sum += value;
        }
        return sum;
    }

    static long BatchExpected(long size, long jobs)
    {
        long sum = 0;
        for (long job = 0; job < jobs; job++)
            sum += IntegerExpected(size + job);
        return sum;
    }

    static long AllocationKernel(long count, long rounds)
    {
        long sum = 0;
        for (long round = 0; round < rounds; round++)
        {
            ComputeNode? head = null;
            for (long index = 0; index < count; index++)
                head = new ComputeNode(index, head);
            while (head != null)
            {
                sum += head.Value;
                head = head.Next;
            }
        }
        return sum;
    }

    static int Main(string[] args)
    {
        if ((args.Length != 3 && args.Length != 4) || !long.TryParse(args[1], out long size) ||
            !long.TryParse(args[2], out long rounds) || size < 1 || size > 200000000 ||
            rounds < 1 || rounds > 10000)
            return 2;
        string mode = args[0];
        int workers = 1;
        if (args.Length == 4 && (!int.TryParse(args[3], out workers) || workers < 1 || workers > 64 || mode != "batch"))
            return 2;
        if ((mode != "integer" && mode != "array" && mode != "allocation" && mode != "batch") ||
            (mode == "array" && size > 8192) || (mode == "allocation" && size > 1000000))
            return 2;
        long expected = mode == "batch" ? BatchExpected(size, rounds) : mode == "integer" ? IntegerExpected(size) :
            mode == "array" ? rounds * size * (size - 1) / 2 + size * rounds * (rounds + 1) / 2 :
            rounds * size * (size - 1) / 2;
        for (int sample = -1; sample < 7; sample++)
        {
            long[] values = mode == "array" ? new long[(int)size] : [];
            for (long index = 0; index < values.LongLength; index++)
                values[index] = index;
            long started = Stopwatch.GetTimestamp();
            long checksum = mode == "batch" ? BatchKernel(size, rounds, workers) : mode == "integer" ? IntegerKernel(size) :
                mode == "array" ? ArrayKernel(values, rounds) : AllocationKernel(size, rounds);
            long elapsed = (Stopwatch.GetTimestamp() - started) * 1000 / Stopwatch.Frequency;
            if (checksum != expected)
                return 1;
            if (sample >= 0)
                Console.WriteLine($"{{\"language\":\"csharp\",\"kernel\":\"{mode}\",\"size\":{size},\"rounds\":{rounds},\"workers\":{workers},\"sample\":{sample},\"milliseconds\":{elapsed},\"checksum\":{checksum}}}");
        }
        return 0;
    }
}
