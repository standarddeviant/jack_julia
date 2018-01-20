# testfile.jl

function passthrough(inp, outp)
    for oidx in 1:size(outp,2)
        # modulo output index to a usable input index
        iidx = mod(oidx, size(inp,2)) + 1

        # copy in-channel (iidx) to out-channel (oidx)
        outp[oidx,:] = inp[iidx,:]
    end
end